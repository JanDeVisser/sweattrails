#!/usr/bin/env python3
"""Garmin Connect helper for sweattrails.
Invoked as subprocess by C code.

Commands:
  login_env          - Auth using GARMIN_EMAIL/GARMIN_PASSWORD env vars,
                       save session tokens to ~/.config/sweattrails/garmin_tokens/
  check              - Validate saved session tokens
  list <limit>       - List recent activities as JSON
  download <id> <path> - Download FIT file (handles ZIP extraction)

All output is single-line JSON on stdout.
Errors return {"status": "error", "message": "..."}.
"""

import json
import os
import sys
import pickle
import zipfile
import tempfile

TOKEN_DIR = os.path.expanduser("~/.config/sweattrails/garmin_tokens")
TOKEN_FILE = os.path.join(TOKEN_DIR, "session.pkl")


def output(obj):
    print(json.dumps(obj, default=str), flush=True)


def error(msg):
    output({"status": "error", "message": str(msg)})
    sys.exit(1)


def save_session(garmin):
    os.makedirs(TOKEN_DIR, exist_ok=True)
    with open(TOKEN_FILE, "wb") as f:
        pickle.dump(garmin, f)


def load_session():
    if not os.path.exists(TOKEN_FILE):
        return None
    with open(TOKEN_FILE, "rb") as f:
        return pickle.load(f)


def cmd_login_env():
    try:
        from garminconnect import Garmin
    except ImportError:
        error("garminconnect not installed. Run: pip install garminconnect")

    email = os.environ.get("GARMIN_EMAIL", "")
    password = os.environ.get("GARMIN_PASSWORD", "")
    if not email or not password:
        error("GARMIN_EMAIL and GARMIN_PASSWORD env vars required")

    try:
        garmin = Garmin(email, password)
        garmin.login()
        save_session(garmin)
        output({"status": "ok", "message": "Login successful"})
    except Exception as e:
        error(f"Login failed: {e}")


def cmd_check():
    garmin = load_session()
    if garmin is None:
        output({"status": "error", "message": "No saved session"})
        return

    try:
        from garminconnect import Garmin  # noqa: F401
        # Try a lightweight API call to validate the session
        garmin.get_full_name()
        output({"status": "ok", "message": "Session valid"})
    except Exception:
        # Try to re-login with saved credentials
        try:
            garmin.login()
            save_session(garmin)
            output({"status": "ok", "message": "Session refreshed"})
        except Exception as e:
            output({"status": "error", "message": f"Session expired: {e}"})


def cmd_list(limit):
    garmin = load_session()
    if garmin is None:
        error("Not authenticated")

    try:
        activities = garmin.get_activities(0, int(limit))
        result = []
        for a in activities:
            result.append({
                "id": a.get("activityId"),
                "name": a.get("activityName", ""),
                "type": a.get("activityType", {}).get("typeKey", ""),
                "start_time": a.get("startTimeLocal", ""),
                "duration": a.get("duration", 0),
                "distance": a.get("distance", 0),
            })
        output({"status": "ok", "activities": result})
    except Exception as e:
        error(f"Failed to list activities: {e}")


def cmd_download(activity_id, output_path):
    garmin = load_session()
    if garmin is None:
        error("Not authenticated")

    try:
        data = garmin.download_activity(int(activity_id),
                                        dl_fmt=garmin.ActivityDownloadFormat.ORIGINAL)
        # ORIGINAL format returns a ZIP containing the FIT file
        with tempfile.NamedTemporaryFile(suffix=".zip", delete=False) as tmp:
            tmp.write(data)
            tmp_path = tmp.name

        try:
            with zipfile.ZipFile(tmp_path, "r") as zf:
                fit_files = [n for n in zf.namelist()
                             if n.lower().endswith(".fit")]
                if fit_files:
                    with zf.open(fit_files[0]) as src:
                        with open(output_path, "wb") as dst:
                            dst.write(src.read())
                    output({"status": "ok", "message": "Downloaded",
                            "path": output_path})
                else:
                    error("No FIT file found in ZIP")
        except zipfile.BadZipFile:
            # Not a ZIP — raw FIT data
            with open(output_path, "wb") as f:
                f.write(data)
            output({"status": "ok", "message": "Downloaded (raw)",
                    "path": output_path})
        finally:
            os.unlink(tmp_path)

    except Exception as e:
        error(f"Download failed: {e}")


def main():
    if len(sys.argv) < 2:
        error("Usage: garmin_helper.py <command> [args...]")

    cmd = sys.argv[1]

    if cmd == "login_env":
        cmd_login_env()
    elif cmd == "check":
        cmd_check()
    elif cmd == "list":
        if len(sys.argv) < 3:
            error("Usage: garmin_helper.py list <limit>")
        cmd_list(sys.argv[2])
    elif cmd == "download":
        if len(sys.argv) < 4:
            error("Usage: garmin_helper.py download <activity_id> <output_path>")
        cmd_download(sys.argv[2], sys.argv[3])
    else:
        error(f"Unknown command: {cmd}")


if __name__ == "__main__":
    main()
