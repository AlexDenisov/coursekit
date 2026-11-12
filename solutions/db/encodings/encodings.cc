#include "db/encodings/encodings.h"

#include <charconv>
#include <cstdio>
#include <cstdlib>

#include "resp/parser/parser.h"

namespace rd {

// ---- CompactHash ----

namespace {

std::optional<size_t> find_field(const Listpack& lp, std::string_view field) {
  auto all = lp.to_vector();
  for (size_t i = 0; i + 1 < all.size(); i += 2) {
    if (all[i] == field) return i;
  }
  return std::nullopt;
}

}  // namespace

bool CompactHash::set(std::string_view field, std::string_view value) {
  if (!converted_) {
    std::optional<size_t> at = find_field(lp_, field);
    bool fits = field.size() <= limits_.hash_max_listpack_value && value.size() <= limits_.hash_max_listpack_value &&
                (at || lp_.size() / 2 + 1 <= limits_.hash_max_listpack_entries);
    if (fits) {
      if (at) {
        lp_.erase(*at + 1);
        lp_.insert(*at + 1, value);
        return false;
      }
      lp_.push_back(field);
      lp_.push_back(value);
      return true;
    }
    auto all = lp_.to_vector();
    for (size_t i = 0; i + 1 < all.size(); i += 2) dict_.insert(all[i], all[i + 1]);
    lp_ = Listpack();
    converted_ = true;
  }
  auto [v, added] = dict_.insert(std::string(field), std::string(value));
  if (!added) *v = std::string(value);
  return added;
}

std::optional<std::string> CompactHash::get(std::string_view field) const {
  if (!converted_) {
    auto at = find_field(lp_, field);
    if (!at) return std::nullopt;
    return lp_.get(*at + 1);
  }
  const std::string* v = dict_.find(field);
  return v ? std::optional<std::string>(*v) : std::nullopt;
}

bool CompactHash::remove(std::string_view field) {
  if (!converted_) {
    auto at = find_field(lp_, field);
    if (!at) return false;
    lp_.erase(*at + 1);
    lp_.erase(*at);
    return true;
  }
  return dict_.erase(field);
}

size_t CompactHash::size() const { return converted_ ? dict_.size() : lp_.size() / 2; }

std::string_view CompactHash::encoding() const { return converted_ ? "hashtable" : "listpack"; }

std::vector<std::pair<std::string, std::string>> CompactHash::items() const {
  std::vector<std::pair<std::string, std::string>> out;
  if (!converted_) {
    auto all = lp_.to_vector();
    for (size_t i = 0; i + 1 < all.size(); i += 2) out.emplace_back(all[i], all[i + 1]);
  } else {
    dict_.for_each([&](const std::string& k, const std::string& v) { out.emplace_back(k, v); });
  }
  return out;
}

// ---- CompactSet ----

bool CompactSet::add(std::string_view member) {
  if (contains(member)) return false;
  int64_t iv;
  bool is_int = resp::parse_int64(member, &iv);
  bool fits_lp = member.size() <= limits_.set_max_listpack_value;
  size_t n = size();
  if (enc_ == Enc::kIntset) {
    if (is_int && n + 1 <= limits_.set_max_intset_entries) {
      intset_.add(iv);
      return true;
    }
    std::vector<std::string> old = members();
    if (!is_int && fits_lp && n + 1 <= limits_.set_max_listpack_entries) {
      enc_ = Enc::kListpack;
      for (auto& m : old) lp_.push_back(m);
    } else {
      enc_ = Enc::kHashtable;
      for (auto& m : old) dict_.insert(m, true);
    }
    intset_ = IntSet();
  } else if (enc_ == Enc::kListpack && (!fits_lp || n + 1 > limits_.set_max_listpack_entries)) {
    for (auto& m : lp_.to_vector()) dict_.insert(m, true);
    lp_ = Listpack();
    enc_ = Enc::kHashtable;
  }
  if (enc_ == Enc::kListpack) {
    lp_.push_back(member);
  } else {
    dict_.insert(std::string(member), true);
  }
  return true;
}

bool CompactSet::remove(std::string_view member) {
  switch (enc_) {
    case Enc::kIntset: {
      int64_t v;
      return resp::parse_int64(member, &v) && intset_.remove(v);
    }
    case Enc::kListpack: {
      auto at = lp_.find(member);
      if (!at) return false;
      lp_.erase(*at);
      return true;
    }
    default:
      return dict_.erase(member);
  }
}

bool CompactSet::contains(std::string_view member) const {
  switch (enc_) {
    case Enc::kIntset: {
      int64_t v;
      return resp::parse_int64(member, &v) && intset_.contains(v);
    }
    case Enc::kListpack:
      return lp_.find(member).has_value();
    default:
      return dict_.find(member) != nullptr;
  }
}

size_t CompactSet::size() const {
  switch (enc_) {
    case Enc::kIntset: return intset_.size();
    case Enc::kListpack: return lp_.size();
    default: return dict_.size();
  }
}

std::string_view CompactSet::encoding() const {
  switch (enc_) {
    case Enc::kIntset: return "intset";
    case Enc::kListpack: return "listpack";
    default: return "hashtable";
  }
}

std::vector<std::string> CompactSet::members() const {
  std::vector<std::string> out;
  switch (enc_) {
    case Enc::kIntset:
      for (size_t i = 0; i < intset_.size(); ++i) out.push_back(std::to_string(intset_.at(i)));
      break;
    case Enc::kListpack:
      out = lp_.to_vector();
      break;
    default:
      dict_.for_each([&](const std::string& k, const bool&) { out.push_back(k); });
  }
  return out;
}

// ---- CompactZSet ----

namespace {

std::string score_text(double v) {
  char buf[64];
  auto r = std::to_chars(buf, buf + sizeof buf, v);
  return std::string(buf, r.ptr);
}

double score_value(const std::string& s) { return std::strtod(s.c_str(), nullptr); }

}  // namespace

bool CompactZSet::add(std::string_view member, double score) {
  bool existed = remove(member);
  if (!converted_ && (member.size() > limits_.zset_max_listpack_value ||
                      lp_.size() / 2 + 1 > limits_.zset_max_listpack_entries)) {
    auto all = lp_.to_vector();
    for (size_t i = 0; i + 1 < all.size(); i += 2) {
      double s = score_value(all[i + 1]);
      scores_.insert(all[i], s);
      order_.insert(s, all[i]);
    }
    lp_ = Listpack();
    converted_ = true;
  }
  if (converted_) {
    scores_.insert(std::string(member), score);
    order_.insert(score, std::string(member));
    return !existed;
  }
  auto all = lp_.to_vector();
  size_t at = 0;
  while (at + 1 < all.size()) {
    double s = score_value(all[at + 1]);
    if (s > score || (s == score && all[at] > member)) break;
    at += 2;
  }
  lp_.insert(at, member);
  lp_.insert(at + 1, score_text(score));
  return !existed;
}

bool CompactZSet::remove(std::string_view member) {
  if (converted_) {
    const double* s = static_cast<const Dict<double>&>(scores_).find(member);
    if (!s) return false;
    order_.erase(*s, member);
    scores_.erase(member);
    return true;
  }
  auto all = lp_.to_vector();
  for (size_t i = 0; i + 1 < all.size(); i += 2) {
    if (all[i] == member) {
      lp_.erase(i + 1);
      lp_.erase(i);
      return true;
    }
  }
  return false;
}

std::optional<double> CompactZSet::score(std::string_view member) const {
  if (converted_) {
    const double* s = scores_.find(member);
    return s ? std::optional<double>(*s) : std::nullopt;
  }
  auto all = lp_.to_vector();
  for (size_t i = 0; i + 1 < all.size(); i += 2) {
    if (all[i] == member) return score_value(all[i + 1]);
  }
  return std::nullopt;
}

size_t CompactZSet::size() const { return converted_ ? scores_.size() : lp_.size() / 2; }

std::string_view CompactZSet::encoding() const { return converted_ ? "skiplist" : "listpack"; }

std::vector<std::pair<std::string, double>> CompactZSet::items() const {
  std::vector<std::pair<std::string, double>> out;
  if (converted_) {
    for (auto* n = order_.first(); n; n = n->next()) out.emplace_back(n->member, n->score);
  } else {
    auto all = lp_.to_vector();
    for (size_t i = 0; i + 1 < all.size(); i += 2) out.emplace_back(all[i], score_value(all[i + 1]));
  }
  return out;
}

}  // namespace rd
