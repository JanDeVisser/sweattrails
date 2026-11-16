# Titles and descriptions of multisport legs

Status: In review

<!--
Status is one of:
- Draft: the Analyst and Jan are working on it.
- Ready: no open questions left; the Developer can start.
- In progress: the Developer is working on it, on branch <branch>.
- In review: the Reviewer is reviewing the branch.
- Done: merged into main.
-->

## Goal

Strava splits a multisport activity into one activity per leg,
transitions included, each with its own title and description: "Welland
Sprint Du | Run 1", "| Ride", "| Run 2", and a race report per leg.
SweatTrails loads the FIT file as one activity with a tab per leg
(`56-multisport.md`), but has one title and description for all of it,
which is whichever leg's Strava happened to match. The user wants each
leg's tab to show that leg's title and description, as on Strava.
Spec 56 deferred this ("Per-leg Strava titles").

## Feature

### What there is now

- An activity's sidecar has one `"strava"` object, `StravaTitle`: ID,
  name, description (`specs/24-strava-titles.md`). The user's own
  `"title"` (spec 11) and `"notes"` (spec 41) win over it.
- **Matching** (`match_strava()`, `StravaTitles.cpp`) takes the store's
  activities by their file names' start, without reading FIT files, and
  gives each the Strava activity starting nearest to it, within 60 s. A
  multisport file is named by its **last** leg's start (spec 57 left it
  so), so the match is whatever Strava activity starts near that.
- The Strava export import (`52-strava-export-other-recordings.md`,
  `give_title()`) moves a Strava title to the file at its start, and
  multisport files were its common case.
- So what a multisport activity shows is luck, not the first leg's:
  - **IM Ottawa** (`2025/08/1754243172`) shows "IM Ottawa Swim" and the
    swim's description, through the export import.
  - **The duathlon** (`2026/06/1782651592`, 8:59:52 local) shows
    "Morning Workout" with no description: Strava's **T2**, which starts
    at 8:59:16, 36 s before the file's name. The run 2 that the file is
    named after starts at 9:01:40 on Strava (below), too far to match.
- The descriptions' background fetch (`StravaBacklog.h`, spec 44) and
  the export's descriptions (spec 45) go by the one Strava ID in the
  sidecar.
- On a leg's tab the title and description are the activity's, and F2
  and the description's pen edit the activity's (spec 56).

### What Strava has

From Jan's export (`activities.csv`, times UTC):

| Strava activity                 | Type    | Start    | FIT leg start |
|---------------------------------|---------|----------|---------------|
| Welland Sprint Du \| Run 1      | Run     | 12:00:02 | 12:00:00      |
| Morning Workout                 | Workout | 12:24:30 | 12:24:30 (T1) |
| Welland Sprint Du \| Ride       | Ride    | 12:25:10 | 12:25:08      |
| Morning Workout                 | Workout | 12:59:16 | 12:59:15 (T2) |
| Welland Sprint Du \| Run 2      | Run     | 13:01:40 | 12:59:51      |
| IM Ottawa Swim                  | Swim    | 10:34:11 | 10:34         |
| Morning Workout                 | Workout | 12:12:27 | 12:12 (T1)    |
| IM Ottawa Ride                  | Ride    | 12:20:16 | 12:20         |
| Afternoon Workout               | Workout | 17:40:21 | 17:40 (T2)    |
| IM Ottawa Run                   | Run     | 17:46:12 | 17:46         |

- **Every leg is a Strava activity, transitions too**, as "Workout",
  with Strava's default name ("Morning Workout").
- **A leg's start on Strava isn't always the FIT leg's**: the
  duathlon's run 2 starts 109 s late on Strava (692 s elapsed, against
  800 s in the FIT file). Matching legs by start within 60 s would miss
  it.

### Matching legs

- Each leg of a multisport activity, transitions included, is matched to
  a Strava activity of its own: the one that **starts within the leg**,
  from 60 s before the leg's start up to the next leg's start (the end,
  for the last leg); the nearest to the leg's start if there are more.
  That finds the duathlon's run 2, and keeps each transition's "Morning
  Workout" on the transition.
- An activity with one session is matched as now.
- Matching legs needs their starts, which the file names don't have.
  The summary file has them since spec 57 (a summary per leg, with its
  start); the Developer picks the source.

### A leg's title and description

- A leg's **title**, lowest to highest priority: the leg's name ("Bike",
  as spec 56 names it), its Strava title, the user's own title for the
  leg. Its **description**: its Strava description, then the user's own
  for the leg. These are "the leg's title" and "the leg's description"
  below.
- **A leg's tab shows the leg's title and description** in the data
  panel, where it now shows the activity's. The summary line still says
  which leg ("Bike, leg 2 of 3").

### The Whole tab

- **Its title**, lowest to highest priority: the start time, as for any
  activity; the legs' **common title**; the user's own title for the
  activity (F2 on Whole), as now.
- The **common title** is the longest common start of the titles of the
  legs that aren't transitions, with trailing spaces and separators
  (`|`, `-`, `·`, `:`) cut: "Welland Sprint Du | Run 1", "Welland Sprint
  Du | Ride" and "Welland Sprint Du | Run 2" give "Welland Sprint Du";
  "IM Ottawa Swim", "IM Ottawa Ride" and "IM Ottawa Run" give "IM
  Ottawa". If that leaves nothing, as for "Morning Run" and "Morning
  Ride", it's the first such leg's title. If no leg has a title of its
  own (Strava's or the user's), there's no common title. This fits how
  Jan names races; it may not fit everyone, and the user's own title is
  the way out.
- The Whole title is the activity's title everywhere else: the header
  and the month's list.
- **Its description**: the user's own description for the activity
  (spec 41), as now, or else the **legs' descriptions** one after the
  other, each as a paragraph starting with the leg's name: "Swim: Super
  choppy…", then "Bike: Slightly under…". Legs without a description are
  left out; with none at all, the Whole tab has none. The data panel
  cuts it at three lines, as any description; the editor shows all of
  it. Jan wants to see if that works in practice.

### Editing

- On a **leg's tab**, F2 and the title's pen edit the **leg's own
  title**, and the description's pen and "Edit description" edit the
  **leg's own description**, with the same editors as now. They're kept
  in the activity's sidecar, per leg, as the activity's own `"title"`
  and `"notes"` are. Saving one empty removes it, which brings back
  Strava's, as now.
- On **Whole**, they edit the activity's own title and description, as
  now. Editing Whole's description starts from what's shown, the legs'
  descriptions, and saves it as the activity's own, which from then on
  wins over the legs'.
- So a leg Strava named badly can be fixed in SweatTrails, and the Whole
  title follows if it's the common title. Nothing is sent to Strava, as
  for any rename.

### Transitions

- Transitions are matched like any leg, and their Strava ID and name are
  saved, so that a transition's "Morning Workout" can't land on a run,
  as the duathlon's T2 does now.
- Their names show nowhere: they have no tab, and they stay "T1", "T2"
  as the Whole tab's laps.
- Their descriptions aren't fetched, in the background or otherwise:
  two requests saved per triathlon, for text that's almost always
  empty.

### The curves' table

A leg's best on the power or pace curve (spec 57) is named by the
**leg's own title**, Strava's or the user's, where it has one:
"Welland Sprint Du | Run 2". Without one, it's named as now: "Whole
title · Run". With the Whole title alone, the duathlon's two runs were
both "Welland Sprint Du · Run".

### The old `"strava"` on multisport files

- The 81 multisport files' sidecars have one `"strava"`, one leg's
  title (or a transition's). The next run of the titles that covers a
  multisport activity matches its legs and **removes** the old
  `"strava"`: its leg, if it was one, gets its title back through its
  own match.
- The Strava export import's `give_title()` (spec 52) **doesn't move
  titles onto multisport files** any more: the legs' matching does that,
  per leg.

### The history

- **Ctrl+Shift+G** (Everything) lists all of Strava and matches every
  leg of every past race, at no extra cost: the list is cheap.
- The legs' **descriptions** come as other descriptions do: in the
  background (spec 44), or by Strava ID from the export import (spec
  45), which looks at the legs' IDs as well as the activities'.
- The export's names aren't used: the list keeps them, as spec 45
  decided.

### Version 1 scope

Everything above. Deferred:

- Sending leg titles and descriptions to Strava *(deferred)*, as for
  any rename.
- A setting or rule for the common title other than the longest common
  start *(deferred)*: the user's own title is the way out for now.

## Open questions

None.

## Architecture

### The sidecar: `"legs"`

- A multisport activity's sidecar gets `"legs"`: an array with an object
  per leg, by its index in `Activity::legs`, transitions included. Each
  object can have `"strava"` (a `StravaTitle`, as the activity's), and
  the user's `"title"` and `"notes"`, the same keys as the activity's
  own. A leg without any of them is `{}`; trailing empty objects, and an
  empty array, are dropped.
- `SidecarTitles` gets `std::vector<LegTitles> legs`, each with
  `strava`, `own` and `own_notes`.
- `save_sidecar_strava()`, `save_sidecar_title()` and
  `save_sidecar_notes()` get an `std::optional<size_t> leg`, empty by
  default: with one, they write leg `leg`'s. Saving a leg's Strava title
  also erases the activity's old `"strava"`.
- `reject_sidecar_strava()` takes the ID off the legs as well.
- `sidecar_stravas(SidecarTitles const &)` (new): all Strava titles in a
  sidecar, each with its leg or none. The backlog, the export's
  descriptions and `strava_fits()` go through it.

### Names on a loaded activity

- `Leg` gets `title`, `original_title`, `notes`, `original_notes`, as
  `Activity` has them. `LegShown` gets `index`, the leg's index in the
  whole's `legs`.
- `read_names()` has three cases:
  - **one session**: as now;
  - **multisport** (`legs` has more than one): each leg's names from its
    leg name, Strava's and the user's; then the activity's title from,
    lowest first, the FIT file's, the service's, the old `.strava`
    file's, the old `"strava"` (until the next titles run removes it),
    the common title, the user's own. The description: the user's own,
    else the legs' descriptions, else the old `"strava"`'s or
    `.strava`'s. So the service's name ("Toronto Multisport") still
    shows on a multisport activity that isn't on Strava; the spec only
    named the start time.
  - **a leg's tab** (`leg` set): leg `leg->index`'s names, from the
    sidecar.
- `leg_activity()` copies the leg's names into the activity it makes.
- `rename()` and `set_notes()` on a leg's tab save the leg's own, with
  `leg->index`.
- In `storage/Legs.h`, pure and testable: `common_title(titles,
  titled)`, `legs_description(names and descriptions)`, and
  `best_leg_title(whole, leg)`: the leg's own title if it has one, else
  "Whole title · Run".
- **The common start is in whole words**: "Run 1", "Ride" and "Run 2"
  have "R" in common, so the common start is cut back to the last space
  or separator when it ends mid-word. That gives "Welland Sprint Du"
  and "IM Ottawa" as the spec says, but "Morning" for "Morning Run" and
  "Morning Ride", where the spec says nothing is left and it's "Morning
  Run". No rule gives both "IM Ottawa" and "Morning Run": both are
  common words followed by a space.
- `split_into_legs()` gives a leg without names its leg name, for an
  activity loaded without a sidecar.

### Matching legs (`StravaTitles.{h,cpp}`)

- `LegSpan { start, end, transition }` and `StoredLegs { stored, legs }`.
  `match_legs(strava, StoredLegs)` gives each leg its Strava activity.
  `StravaMatch` gets `std::optional<size_t> leg` and `bool transition`.
- **One change to the spec's rule**: "the nearest to the leg's start if
  there are more" gives the duathlon's run 2 Strava's T2 (35 s before
  it) rather than its run 2 (109 s after it). So each Strava activity
  first goes to the leg, of those whose window it starts in, whose start
  is nearest; then each leg takes the nearest of the activities that went
  to it. T2 goes to T2, and run 2 gets "| Run 2".
- **The legs' starts come from the summary file**: a summary per leg,
  with its start and sport. The last leg's end is its start plus its
  moving time, as the summary has no elapsed time: a slightly short
  window, on the safe side. A file the summary file doesn't have as it
  is now (new since the last update, which runs in chunks after the
  inbox) is read with `read_file_summaries()`, so the same code makes
  them. `summary_file()` (new, from `summary_files()`) gives one file's
  size and time to compare.
- `update_strava_titles()` takes `Storage &` (`read_file_summaries()`
  loads activities), matches files with one summary as now and the
  others with `match_legs()`, and saves per leg.
- **Transitions are saved with an empty description**, as a description
  fetched empty is. So neither the backlog nor the export import asks for
  them, with no special case there.

### Descriptions and the export

- `Undescribed::stored` becomes places: `StravaPlace`, a `StoredStart`
  with an `std::optional<size_t> leg`. The export's `Missing` gets a
  `leg` too. `save_description()` reads the sidecar again and saves into
  the leg's.
- `strava_fits()` lists the legs' IDs too, so a leg's file in the export
  is the stored file, not one to queue.
- `queue_missing()` doesn't call `give_title()` when Strava's file is
  multisport (`RecordingInfo::sport`); it still rejects the ID from the
  wrong activity.

### The screen

- `ActivityDisplay::show_tab()`: leaving a leg's tab reads the whole's
  names from the sidecar again (`read_names()`) rather than copying them,
  since a leg's rename can change the common title.
- `notif_titles_changed()` reads the names of both the tab shown and the
  whole.
- The curves' `ride_name` returns `best_leg_title()` for a leg's best;
  `best_name()` (`widget/Summaries.cpp`) only falls back to the leg's
  name when there's no name. `SummariesScreen::ride_names` is keyed by
  file and leg, as two legs of a file can each set bests.
- Tests: `TestLeg` and `add_leg()` move from `LegsTest.cpp` to
  `TestFit.h`, to build multisport files in other tests.

## Test plan

- `StravaTitlesTest.cpp`:
  - `match_legs()` with the duathlon's and IM Ottawa's times from the
    spec: each leg, transitions included, gets its own Strava activity;
    run 2 gets "| Run 2", not T2; an activity starting after the last
    leg's end isn't matched.
  - `update_strava_titles()` on a store with a multisport file made with
    `FITBuilder`: the legs' titles are saved, the old `"strava"` is
    removed, transitions have an empty description; a file not yet in
    the summary file is matched too.
- `LegsTest.cpp`:
  - `common_title()`: the Welland and IM Ottawa examples, separators and
    spaces cut, "Morning Run"/"Morning Ride" gives the first, transitions
    left out, nothing without titles.
  - `legs_description()`: "Swim: …" paragraphs, legs without one left
    out, none at all gives none.
  - Loading a multisport file with a sidecar: each leg's title and
    description by priority; Whole's title (start time, service, common,
    own) and description (legs', own); `leg_activity()` has the leg's
    names.
  - `rename()` and `set_notes()` on a leg's tab save the leg's own, and
    empty brings back Strava's.
  - `best_leg_title()`: the leg's own title, else "Whole · Run".
- `ActivityLoadTest.cpp`: `"legs"` round-trips through the sidecar
  functions; trailing empty legs are dropped; `reject_sidecar_strava()`
  takes a leg's ID off.
- `StravaBacklogTest.cpp`: a leg's ID is in the backlog and its
  description is saved in the leg; a transition's isn't.
- `StravaExportTest.cpp`: `import_descriptions()` fills a leg's;
  `queue_missing()` doesn't give a multisport file a title, and doesn't
  queue a leg's file.
- `SummariesScreenTest.cpp`: a leg's best is named by `ride_name` as is.
- By hand: the duathlon and IM Ottawa after Ctrl+G: the tabs' titles and
  descriptions, Whole's, F2 and the pen on a leg, the curves' names.

## Review notes

*Filled in by the Reviewer.*
