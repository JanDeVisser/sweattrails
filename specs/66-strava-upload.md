# Uploading to Strava

Status: In progress, on branch feature/66-strava-upload

<!--
Status is one of:
- Draft: the Analyst and Jan are working on it.
- Ready: no open questions left; the Developer can start.
- In progress: the Developer is working on it, on branch <branch>.
- In review: the Reviewer is reviewing the branch.
- Done: merged into main.
-->

## Goal

The user can send an activity's FIT file from SweatTrails to Strava.

The usual case is a file that needed fixing first, such as a ride with
gaps filled in SweatTrails (`specs/30-fill-gaps.md`,
`specs/65-fill-from-paused-track.md`). If Strava already has the broken
version, Jan deletes it on strava.com and then uploads the fixed file.
Strava's API can't replace an activity's file and can't delete an
activity, so deleting by hand stays part of the process. SweatTrails
makes the rest of it easy.

## Feature

### What there is now

- **The Strava client** (`storage/Strava.*`, `specs/24-strava-titles.md`)
  logs in with OAuth 2, using the user's own Strava app, and only reads:
  its scope is `activity:read_all` (`StravaScopes`, `Strava.h`). It lists
  activities, fetches descriptions, retries, and copes with the rate limits
  (`specs/44-rate-limits.md`).
- **Matching**: an activity in the store that matches a Strava activity,
  within 60 seconds of its start time, gets `"strava": { "id", "name",
  "description" }` in its sidecar. That only happens for the last
  `strava.days` (30 by default) with every download, or for the whole
  history with `st-strava-titles`. So an activity without a `"strava"` key
  either isn't on Strava, or hasn't been matched yet.
- **Http** (`src/App/Http.h`): `HttpRequest` has a method, headers and a
  string body. There's no multipart support, but a multipart body can be
  built as a string, with its own `Content-Type` header.
- `storage/strava/Misc.h` already has the `Upload` type that Strava returns
  (`id`, `external_id`, `error`, `status`, `activity_id`), which the old Zig
  app had. Nothing uses it.
- **Changed FIT files**: filling gaps (`specs/30-fill-gaps.md`,
  `specs/65-fill-from-paused-track.md`) patches the FIT file in place, and
  keeps the watch's file as `<start>.fit.original`.
- **Several recordings** (`specs/31-multiple-recordings.md`): one activity
  can have two FIT files, recorded on two devices.

### Strava's upload API

From [developers.strava.com](https://developers.strava.com/docs/reference/)
and its [uploads guide](https://developers.strava.com/docs/uploads),
November 2026:

- `POST /api/v3/uploads`, `multipart/form-data`, with `file` and
  `data_type` (`fit`, `fit.gz`, `tcx`, `gpx`, ..., case insensitive), and
  optionally `name`, `description`, `sport_type`, `trainer`, `commute`
  and `external_id`. It needs the `activity:write` scope. A malformed
  submission is a 400.
- The upload is processed asynchronously. The answer, a 201, is an
  `Upload` object (`"status": "Your activity is still being
  processed."`). `GET /api/v3/uploads/{id}` is asked, at most once a
  second, until `activity_id` is set, or `error` is. Until then the new
  activity doesn't show up in the API's other requests.
- A duplicate is an upload error, not an HTTP error, seen when polling:
  `"error": "Test_Walk.gpx duplicate of activity 21234316"`.
- `GET /api/v3/activities/{id}` returns an activity the athlete owns. The
  documentation only says a missing one is "4xx", without saying which.
- **There's no way to delete an activity**: the API has create, get,
  update and the lists, but no `DELETE`. It also can't replace an
  activity's file. So a fixed file only gets on Strava after the user
  deletes the old activity on strava.com.
- Uploads and polls count against the rate limits.

### Logging in

The login asks for `activity:read_all,activity:write`, always
(`StravaScopes`), for titles and uploads alike. A login that has only
`activity:read_all` doesn't work for an upload: Strava answers 401 (or
403), and the upload says so, with "Log in to Strava again to upload?",
which starts `st-strava-login` on Yes. The README's Strava section says
the login needs write access for uploads.

### Uploading

- One activity at a time: the activity on display, from the command
  palette, "Upload to Strava". A batch upload is *(deferred)*: fixing
  is done one activity at a time, and SweatTrails only knows an activity
  is already on Strava once it has been matched.
- What goes is the activity's FIT file as it is in the store: a filled
  file is uploaded with its fills, not `<start>.fit.original`.
- **The old activity first.** If the sidecar has a `"strava"` ID,
  SweatTrails asks Strava for that activity before uploading:
  - Strava still has it: no upload. SweatTrails asks "Strava still has
    this activity. Open it to delete it?", and on Yes opens
    `https://www.strava.com/activities/<id>` in the browser
    (`open_browser()`). The user deletes it there and runs the upload
    again. The API can't delete it.
  - Strava doesn't have it (a 404): the upload goes ahead.
  - The check fails some other way (no connection, the rate limit):
    no upload, with the error, as for the other Strava requests.
- **Several recordings** (`specs/31-multiple-recordings.md`): the
  recording on display goes, as Tab picks it
  (`specs/32-switching-recordings.md`). Filling a gap works on the
  recording shown too, so the file that was fixed is the one uploaded.
  The check for the old activity uses that recording's `"strava"` ID.
- **Multisport activities** (`specs/56-multisport.md`) aren't uploaded
  yet: on a leg, the message says "Not on a single leg: press Tab to go
  to Whole.", as filling gaps does; on Whole, "Uploading a multisport
  activity to Strava isn't supported yet." Their Strava titles are per
  leg, which the check and the sidecar update don't handle.
- **What goes with the file**:
  - `name`: the activity's title, the main recording's, as SweatTrails
    shows it, from its usual priority
    (`specs/24-strava-titles.md`): the user's title, Strava's, the
    service's.
  - `description`: the sidecar's `"strava"` description, if it has one,
    so the deleted activity's description comes back.
  - The sport type is left to Strava, from the FIT file.
  - Gear, privacy, photos, kudos and comments of a deleted activity are
    lost. That's a limit of Strava's API.
- **Waiting for Strava**: the status bar says it's uploading. SweatTrails
  polls the upload once a second until it has an activity ID or an error,
  for up to a minute. After that it gives up, saying Strava is still
  processing and the activity will show up on Strava once it's done.
- **After the upload**: the sidecar's `"strava"` becomes the new
  activity: `{ "id": <new id>, "name": <name sent>, "description":
  <description sent> }`. The rest of the sidecar is kept. The message
  says "Uploaded to Strava." Every other recording of the activity
  whose `"strava"` ID was the old one gets the new `"strava"` as well:
  a titles run gives one Strava activity to all the recordings of a
  ride, and none of them should point at the deleted one. A titles run then matches it at once, and
  the next upload's check asks about the right ID.

### Errors

- A duplicate (an activity on Strava that the sidecar doesn't know about,
  because it was never matched, say): the message says Strava already has
  it, as activity N, and asks "Open it to delete it?", as above. The
  sidecar isn't changed.
- Strava rejects the file for any other reason: the message gives
  Strava's `error`.
- No connection, a failed request, the rate limit: as for the other
  Strava requests (`specs/44-rate-limits.md`). A failed upload changes
  nothing in the store.
- No Strava login: as for the titles, the user is asked to log in.
- A user without a `strava.client_id` doesn't see the command.

### Checked first by the Developer

- What `GET /activities/{id}` answers for a deleted activity (presumably
  404), and for a login without `activity:write` on an upload.
- Whether a duplicate can also be rejected at `POST` time, with a 400,
  rather than when polling.

Found, 2026-11-22: `GET /api/v3/activities/21088108244`, an activity
deleted on strava.com, is `404` with `{"message":"Record Not Found",
"errors":[]}`. The upload with a read-only login and the duplicate weren't
tried from here: the code takes a 401 or 403 to the upload as a missing
write scope, and a duplicate both at the POST and when polling.

### Version 1 scope

In: always requesting write access at login; uploading the recording on
display, as it is in the store; the check for the old activity, with
opening it in the browser; the name and description sent; waiting for
the result; the new ID in the sidecars of the recordings that had the old one; the
errors above.

*(Deferred)*:

- Uploading several activities at once.
- Deleting the old activity from SweatTrails, which Strava's API can't
  do.

## Open questions

None.

## Architecture

### Multipart, in `Http.h`

Building a `multipart/form-data` body is general, so it goes next to
`form_encode()`:

```cpp
struct FormPart {
    std::string name { };
    std::string value { };     // The text, or the file's bytes
    std::string file_name { }; // Empty for a text field
};

// The body, with `boundary` between the parts, and the Content-Type
// header that goes with it.
std::string multipart_encode(std::string_view boundary, std::vector<FormPart> const &parts);
std::string multipart_content_type(std::string_view boundary);
```

The boundary is random (`random_token()`), passed in so that tests can
fix it.

### The Strava client, `storage/Strava.*`

- `StravaScopes` becomes `"activity:read_all,activity:write"`.
- The request part of `get()` becomes `send(HttpRequest)`: the token
  refresh, the Authorization header, the usage, 401 as `NotLoggedIn`.
  `get()` keeps the retries; a POST isn't retried, because an upload
  Strava got but didn't answer would come back as a duplicate of itself.
- `StravaError::Kind` gets `Forbidden`: a 401 or 403 to an upload or its
  poll, which means a login without `activity:write` ("Log in to Strava
  again to upload?"), as against `NotLoggedIn` for a missing or refused
  login on the check before it.
- New methods:

```cpp
// Whether the athlete still has activity `id`: a 404 is no.
StravaResult<bool> has_activity(u64 id);

struct StravaUpload {
    u64                id { 0 };
    std::optional<u64> activity_id { }; // Set once Strava is done
    std::string        error { };       // Set if Strava rejected it
};

// Sends a FIT file, `fit` its bytes, with a name and description.
StravaResult<StravaUpload> upload(std::string const &fit, std::string const &name, std::optional<std::string> const &description);
// How upload `id` is doing.
StravaResult<StravaUpload> upload_status(u64 id);
```

`StravaUpload` is parsed by hand rather than with `strava::Upload` from
`Misc.h`, whose fields can't be `null`, as `activity_id` and `error` are
until Strava is done. `Misc.h`'s `Upload` stays unused.

- `duplicate_of(std::string_view error) -> std::optional<u64>`: the ID in
  "... duplicate of activity 21234316", or in a link to
  `/activities/21234316`, which Strava's own site shows. A 400 to the POST
  whose `error` says duplicate is a duplicate as well, in case Strava
  rejects it at once.

### The upload, `storage/StravaUpload.{h,cpp}`

The steps, without the UI, so that tests can run them against
`FakeStrava`:

```cpp
struct UploadRequest {
    fs::path                   fit { };          // The recording on display
    fs::path                   sidecar { };      // Its sidecar
    std::string                name { };
    std::optional<std::string> description { };
    std::vector<fs::path>      others { };       // The other recordings' sidecars
};

struct UploadOutcome {
    enum class Kind {
        Uploaded,        // `activity_id` is the new one
        StillOnStrava,   // The sidecar's activity is still there: `activity_id`
        Duplicate,       // Strava has it already, as `activity_id`
        Rejected,        // `message` is Strava's error
        StillProcessing, // Not done after a minute
    };
    Kind        kind { Kind::Uploaded };
    u64         activity_id { 0 };
    std::string message { };
};

// Checks for the old activity, uploads, polls every `poll_pause` up to
// `polls` times, and saves the new activity in the sidecars.
StravaResult<UploadOutcome> upload_to_strava(Strava &strava, UploadRequest const &request, UploadPacing const &pacing);

// The new activity in `request.sidecar`, and in each of `request.others`
// whose "strava" ID was `old_id`. The rest of each sidecar stays.
std::expected<void, LibCError> save_uploaded(UploadRequest const &request, std::optional<u64> old_id, StravaTitle const &uploaded);
```

`UploadPacing` holds the pause, the number of polls (60) and a `wait`
function: without one nothing waits, as in the tests.

### The app

- `st-upload-to-strava`, "Upload to Strava", in the palette, without a
  key, `.when()` it's on the activity screen and `strava_client()` has a
  client ID. `cmd_upload_to_strava` builds the `UploadRequest` from the
  activity on display: `fit_path()`, `sidecar`, `title` (the activity's,
  whichever recording is shown), the Strava description from the
  sidecar, and the sidecars of the other recordings of `main_of()`.
- A new job, `UploadToStrava`, runs `upload_to_strava()` on the job
  thread. Polling waits there, a second at a time, up to a minute;
  other jobs wait behind it.
- A new notification, `UploadedToStrava`, with what to say, the Strava
  activity to offer to open, and whether to offer a login. The UI then
  shows a message, or "Open it to delete it?" (Yes runs
  `open_browser()`), or "Log in to Strava again to upload?" (Yes submits
  `st-strava-login`). After an upload it also sends `TitlesChanged` for
  the activity, so that the display reads its sidecar again.
- The status bar says "Uploading to Strava…" while the job runs, and
  "Waiting for Strava: <n> s" while polling, from a `Downloading`
  notification with source "Strava".
- The README's Strava section says the login asks for write access, for
  uploads.

No new widgets: the query and message boxes there are do.

## Test plan

- `HttpTest`: `multipart_encode()` gives the parts, with their
  Content-Disposition, a file part with its file name and
  `application/octet-stream`, and the closing boundary.
- `StravaTest`: `authorize_url()` asks for both scopes;
  `has_activity()` is true for a 200 and false for a 404, and an error
  for a 500; `upload()` posts multipart with `file`, `data_type=fit`,
  `name` and `description`, and no `description` without one; a 401 or
  403 to it is `Forbidden`; a POST isn't retried; `upload_status()`
  parses a pending upload (nulls), a done one and an error;
  `duplicate_of()` for Strava's text, a link, and something else.
- `StravaUploadTest` (new), against `FakeStrava`, in a `TempDir` with
  sidecars:
  - Without a `"strava"` ID: uploads at once, polls until done, and saves
    the new ID, name and description, keeping the rest of the sidecar.
  - With one Strava still has: `StillOnStrava`, nothing posted, nothing
    saved.
  - With one Strava doesn't have: uploads, and the other recordings with
    the old ID get the new one; one with a different ID doesn't.
  - A duplicate seen when polling, and at the POST: `Duplicate` with its
    ID, nothing saved.
  - Another error: `Rejected` with Strava's text.
  - Never done: `StillProcessing` after the polls, nothing saved.
  - The check failing (a 500, a 429): the error, nothing posted.
- What can't be tested automatically: the palette command and the boxes
  it opens, opening the browser, and Strava itself. Those are checked by
  hand against Strava: see Checked first.

## Review notes

*Filled in by the Reviewer.*
