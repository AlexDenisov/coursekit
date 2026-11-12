// Written by tools/sync_curriculum.py from curriculum.json; don't edit.
window.CURRICULUM = {
 "title": "My course",
 "about": "The single source of truth for the course: levels, tracks (goals), domains (directories), exercises, edges and presets. Read by docs/map.html (through docs/curriculum.js), tools/progress.py and tools/sync_curriculum.py. Run `python3 tools/sync_curriculum.py` after every edit. The exercises below are placeholders that show the format; replace them when you bootstrap a course. Rule: an exercise depends only on exercises at its level or below.",
 "progress": {
  "runner": "command",
  "test": "",
  "optional_test": "",
  "build": "",
  "junit": "",
  "timeout": 600,
  "start_marker": "TODO(you"
 },
 "levels": [
  {
   "id": "basic",
   "name": "Basic",
   "meaning": "first contact with the topic; small, self-contained pieces"
  },
  {
   "id": "intermediate",
   "name": "Intermediate",
   "meaning": "standard material; pieces start to combine"
  },
  {
   "id": "advanced",
   "name": "Advanced",
   "meaning": "specialised, systems-level, or close to research"
  }
 ],
 "tracks": [
  {
   "id": "foundations",
   "color": "--p1",
   "name": "Foundations",
   "goal": "the building blocks every other track uses"
  },
  {
   "id": "application",
   "color": "--p3",
   "name": "Application",
   "goal": "combine the building blocks into something real"
  }
 ],
 "domains": [
  {
   "id": "basics",
   "name": "Basics"
  },
  {
   "id": "core",
   "name": "Core"
  },
  {
   "id": "projects",
   "name": "Projects"
  }
 ],
 "exercises": [
  {
   "id": "basics/first_block",
   "title": "First block",
   "level": "basic",
   "tracks": [
    "foundations"
   ],
   "kind": "concept",
   "about": "placeholder: the smallest useful piece; everything else builds on it"
  },
  {
   "id": "basics/second_block",
   "title": "Second block",
   "level": "basic",
   "tracks": [
    "foundations"
   ],
   "kind": "concept",
   "about": "placeholder: a second independent piece"
  },
  {
   "id": "basics/helpers",
   "title": "Helpers",
   "level": "basic",
   "tracks": [
    "foundations"
   ],
   "kind": "infra",
   "given": true,
   "about": "placeholder: given code (read it, don't write it): parsers, printers, test data"
  },
  {
   "id": "core/combination",
   "title": "Combination",
   "level": "intermediate",
   "tracks": [
    "foundations",
    "application"
   ],
   "kind": "concept",
   "about": "placeholder: uses both blocks"
  },
  {
   "id": "core/fast_path",
   "title": "Fast path",
   "level": "intermediate",
   "tracks": [
    "application"
   ],
   "kind": "perf",
   "about": "placeholder: an optimisation with a benchmark"
  },
  {
   "id": "projects/capstone",
   "title": "Capstone",
   "level": "advanced",
   "tracks": [
    "application"
   ],
   "kind": "project",
   "milestone": true,
   "about": "placeholder: the milestone; runs on real data, built entirely from your own code"
  }
 ],
 "edges": [
  [
   "basics/first_block",
   "core/combination",
   "req"
  ],
  [
   "basics/second_block",
   "core/combination",
   "part",
   "only one function"
  ],
  [
   "basics/helpers",
   "core/combination",
   "learn"
  ],
  [
   "core/combination",
   "core/fast_path",
   "req"
  ],
  [
   "core/combination",
   "projects/capstone",
   "req"
  ],
  [
   "core/fast_path",
   "projects/capstone",
   "real",
   "only the real-data test"
  ],
  [
   "basics/second_block",
   "projects/capstone",
   "idea",
   "helpful background"
  ]
 ],
 "presets": [
  {
   "label": "Capstone",
   "targets": [
    "projects/capstone"
   ]
  },
  {
   "label": "All basics",
   "level": "basic"
  }
 ],
 "_hash": "adaf09dd6cf2"
};
