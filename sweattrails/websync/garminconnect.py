#!/usr/bin/python3
# -*- coding: utf-8 -*-
#
# Copyright (c) 2018 Jan de Visser (jan@sweattrails.com)
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the Free
# Software Foundation; either version 2 of the License, or (at your option)
# any later version.
#
# This program is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
# more details.
#
# You should have received a copy of the GNU General Public License along
# with this program; if not, write to the Free Software Foundation, Inc., 51
# Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
#

"""
Based on:

File: gcexport.py
Original author: Kyle Krafka (https://github.com/kjkjava/)
Date: April 28, 2015
Fork author: Michael P (https://github.com/moderation/)
Date: August 25, 2018

Description:    Use this script to export your fitness data from Garmin Connect.
                See README.md for more information.

Activity & event types:
    https://connect.garmin.com/modern/main/js/properties/event_types/event_types.properties
    https://connect.garmin.com/modern/main/js/properties/activity_types/activity_types.properties
"""

from datetime import datetime, timedelta
from os import remove, stat
from os.path import isfile, join
# from subprocess imp call
# from xml.dom.minidom imp parseString
import http.cookiejar
import json
import re
import sys
import traceback
import urllib.error
import urllib.parse
import urllib.request
import zipfile

import gripe
import gripe.db
import grizzle
import grumble

logger = gripe.get_logger(__name__)


class GCActivity(grumble.Model):
    activity_id = grumble.TextProperty(is_key=True)


class Download:
    URL = None
    FORMAT = "{:s}{:s}?full=true"
    FILE_MODE = "w"
    DATA_SUFFIX = None

    def __init__(self, gc, activity_id):
        self._gc = gc
        self.id = activity_id
        self.data_filename = None
        self.download_url = None
        self._data = None

    def _pre_download(self):
        return True

    def _handle_error(self, err):
        return None

    def validate(self):
        pass

    def handle_error(self, err):
        ret = self._handle_error(err)
        if ret is None:
            raise Exception("Failed. Got an unexpected HTTP error %s for %s", err.code, self.download_url)
        return ret

    def download(self):
        self.data_filename = join(self._gc.inbox, self.id + "_activity." + self.DATA_SUFFIX)
        self.download_url = self.FORMAT.format(self.URL, self.id)
        if isfile(self.data_filename):
            logger.info("\tData file already exists; skipping...")
            return
        if not self._pre_download():
            return

        # Download the data file from Garmin Connect. If the download fails (e.g., due to timeout),
        # this script will die, but nothing will have been written to disk about this activity, so
        # just running it again should pick up where it left off.
        try:
            self._data = self._gc.http_req(self.download_url)
        except urllib.error.HTTPError as err:
            self._data = self.handle_error(err)

        # Persist file
        self._gc.write_to_file(self.data_filename, self._gc.decoding_decider(self._data), self.FILE_MODE)
        self.validate()


class GPXDownload(Download):
    URL = "https://connect.garmin.com/modern/proxy/download-service/export/gpx/activity/"
    DATA_SUFFIX = "gpx"

    def validate(self):
        if self._data:
            pass
            # Validate GPX data. If we have an activity without GPS data (e.g., running on a
            # treadmill), Garmin Connect still kicks out a GPX (sometimes), but there is only
            # activity information, no GPS data. N.B. You can omit the XML parse (and the
            # associated log messages) to speed things up.
            # gpx = parseString(self._data)
            # if gpx.getElementsByTagName("trkpt"):
            #     logger.info("Done. GPX data saved.")
            # else:
            #     logger.info("Done. No track points found.")


class TCXDownload(Download):
    URL = "https://connect.garmin.com/modern/proxy/download-service/export/tcx/activity/"
    DATA_SUFFIX = "tcx"

    def _handle_error(self, err):
        if err.code == 500:
            # Garmin will give an internal server error (HTTP 500) when downloading TCX files
            # if the original was a manual GPX upload. Writing an empty file prevents this file
            # from being redownloaded, similar to the way GPX files are saved even when there
            # are no tracks. One could be generated here, but that's a bit much. Use the GPX
            # format if you want actual data in every file, as I believe Garmin provides a GPX
            # file for every activity.
            logger.info("Writing empty file since Garmin did not generate a TCX file for this activity...")
            return ""


class FITDownload(Download):
    URL = "http://connect.garmin.com/proxy/download-service/files/activity/"
    DATA_SUFFIX = "zip"
    FILE_MODE = "wb"

    def __init__(self, gc, activity_id):
        super(FITDownload, self).__init__(gc, activity_id)
        self.fit_filename = None

    def _handle_error(self, err):
        if err.code == 404:
            # For manual activities (i.e., entered in online without a file upload), there is
            # no original file.
            logger.info("Writing empty file since there was no original activity data...")
            return ""

    def validate(self):
        logger.info("Unzipping and removing original file")
        if stat(self.data_filename).st_size > 0:
            zip_file = open(self.data_filename, "rb")
            z = zipfile.ZipFile(zip_file)
            for name in z.namelist():
                z.extract(name, self._gc.inbox)
            zip_file.close()
        remove(self.data_filename)


class GarminConnect:
    SCRIPT_VERSION = "2.0.0"
    CURRENT_DATE = datetime.now().strftime("%Y-%m-%d")
    ACTIVITIES_DIRECTORY = "./" + CURRENT_DATE + "_garmin_connect_export"

    # Maximum number of activities you can request at once.  Set and enforced by Garmin.
    LIMIT_MAXIMUM = 1000

    WEBHOST = "https://connect.garmin.com"
    REDIRECT = "https://connect.garmin.com/post-auth/login"
    BASE_URL = "http://connect.garmin.com/en-US/signin"
    SSO = "https://sso.garmin.com/sso"
    CSS = "https://static.garmincdn.com/com.garmin.connect/ui/css/gauth-custom-v1.2-min.css"

    DATA = {
        "service": REDIRECT,
        "webhost": WEBHOST,
        "source": BASE_URL,
        "redirectAfterAccountLoginUrl": REDIRECT,
        "redirectAfterAccountCreationUrl": REDIRECT,
        "gauthHost": SSO,
        "locale": "en_US",
        "id": "gauth-widget",
        "cssUrl": CSS,
        "clientId": "GarminConnect",
        "rememberMeShown": "true",
        "rememberMeChecked": "false",
        "createAccountShown": "true",
        "openCreateAccount": "false",
        "usernameShown": "false",
        "displayNameShown": "false",
        "consumeServiceTicket": "false",
        "initialFocus": "true",
        "embedWidget": "false",
        "generateExtraServiceTicket": "false",
    }

    logger.debug(urllib.parse.urlencode(DATA))

    # URLs for various services.
    URL_GC_LOGIN = "https://sso.garmin.com/sso/login?" + urllib.parse.urlencode(DATA)
    URL_GC_POST_AUTH = "https://connect.garmin.com/modern/activities?"
    URL_GC_PROFILE = "https://connect.garmin.com/modern/profile"
    URL_GC_USERSTATS = (
        "https://connect.garmin.com/modern/proxy/userstats-service/statistics/"
    )
    URL_GC_LIST = "https://connect.garmin.com/modern/proxy/activitylist-service/activities/search/activities?"
    URL_GC_ACTIVITY = "https://connect.garmin.com/modern/proxy/activity-service/activity/"
    URL_DEVICE_DETAIL = (
        "https://connect.garmin.com/modern/proxy/device-service/deviceservice/app-info/"
    )

    _DOWNLOADERS = {
        "gpx": GPXDownload,
        "tcx": TCXDownload,
        "original": FITDownload
    }

    @staticmethod
    def hhmmss_from_seconds(sec):
        """Helper function that converts seconds to HH:MM:SS time format."""
        if isinstance(sec, float):
            formatted_time = str(timedelta(seconds=int(sec))).zfill(8)
        else:
            formatted_time = "0.000"
        return formatted_time

    @staticmethod
    def kmh_from_mps(mps):
        """Helper function that converts meters per second (mps) to km/h."""
        return str(mps * 3.6)

    @staticmethod
    def write_to_file(filename, content, mode):
        """Helper function that persists content to file."""
        write_file = open(filename, mode)
        write_file.write(content)
        write_file.close()

    def __init__(self, user):
        self.user = user
        self.username = None
        self.password = None
        self.format = 'original'
        self.cookie_jar = http.cookiejar.CookieJar()
        self.opener = urllib.request.build_opener(urllib.request.HTTPCookieProcessor(self.cookie_jar))
        if isinstance(user, str):
            self.inbox = user
        else:
            user_dir = gripe.user_dir(user.uid())
            self.inbox = join(user_dir, "inbox")
        self._user_stats = None

    def credentials(self, username, password):
        self.username = username
        self.password = password

    def set_format(self, fmt):
        if fmt not in ('original', 'gpx', 'tcx'):
            raise ValueError("Unrecognized Garmin Connect Export Format '%s'" % fmt)
        else:
            self.format = fmt

    def decoding_decider(self, data):
        """Helper function that decides if a decoding should happen or not."""
        if self.format == "original":
            # An original file (ZIP file) is binary and not UTF-8 encoded
            data = data
        elif data:
            # GPX and TCX are textfiles and UTF-8 encoded
            data = data.decode()
        return data

    def http_req(self, url, post=None, headers=None):
        """
            Helper function that makes the HTTP requests.
            url is a string, post is a dictionary of POST parameters,
            headers is a dictionary of headers.
        """
        logger.debug("http_req(%s, %s, %s)", url, post, headers)
        request = urllib.request.Request(url)
        # Tell Garmin we're some supported browser.
        request.add_header(
            "User-Agent",
            "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, \
            like Gecko) Chrome/54.0.2816.0 Safari/537.36",
        )
        if headers:
            for header_key, header_value in headers.items():
                request.add_header(header_key, header_value)
        if post:
            post = urllib.parse.urlencode(post)
            post = post.encode("utf-8")  # Convert dictionary to POST parameter string.
        # print("request.headers: " + str(request.headers) + " COOKIE_JAR: " + str(COOKIE_JAR))
        # print("post: " + str(post) + "request: " + str(request))
        response = self.opener.open(request, data=post)

        if response.getcode() == 204:
            # For activities without GPS coordinates, there is no GPX download (204 = no content).
            # Write an empty file to prevent redownloading it.
            print("Writing empty file since there was no GPX activity data...")
            return ""
        elif response.getcode() != 200:
            raise Exception("Bad return code (" + str(response.getcode()) + ") for: " + url)
        # print(response.getcode())

        return response.read()

    def login(self):
        self.http_req(self.URL_GC_LOGIN)
        logger.debug("Finish login page")

        # Now we'll actually login.
        # Fields that are passed in a typical Garmin login.
        post_data = {
            "username": self.username,
            "password": self.password,
            "embed": "true",
            "lt": "e1s1",
            "_eventId": "submit",
            "displayNameRequired": "false",
        }

        login_response = self.http_req(self.URL_GC_LOGIN, post_data).decode()
        logger.debug("Finish login post")

        # extract the ticket from the login response
        pattern = re.compile(r".*\?ticket=([-\w]+)\";.*", re.MULTILINE | re.DOTALL)
        match = pattern.match(login_response)
        if not match:
            raise Exception(
                "Did not get a ticket in the login response. Cannot log in. Did \
        you enter the correct username and password?"
            )
        login_ticket = match.group(1)
        logger.debug("Login ticket=%s", login_ticket)

        logger.debug("Request authentication URL: %s, ticket %s", self.URL_GC_POST_AUTH, login_ticket)
        self.http_req(self.URL_GC_POST_AUTH + "ticket=" + login_ticket)
        logger.info("Finished authentication")

    def count(self):
        # If the user wants to download all activities, query the userstats
        # on the profile page to know how many are available
        logger.log("Getting display name and user stats via: %s", self.URL_GC_PROFILE)
        profile_page = self.http_req(self.URL_GC_PROFILE).decode()
        # write_to_file(args.directory + '/profile.html', profile_page, 'a')

        # extract the display name from the profile page, it should be in there as
        # \"displayName\":\"eschep\"
        pattern = re.compile(
            r".*\\\"displayName\\\":\\\"([-.\w]+)\\\".*", re.MULTILINE | re.DOTALL
        )
        match = pattern.match(profile_page)
        if not match:
            raise Exception("Did not find the display name in the profile page.")
        display_name = match.group(1)
        logger.log("displayName=%s", display_name)

        user_stats = self.http_req(self.URL_GC_USERSTATS + display_name)
        print("Finished display name and user stats ~~~~~~~~~~~~~~~~~~~~~~~~~~~")

        # Persist JSON
        # write_to_file(ARGS.directory + "/userstats.json", USER_STATS.decode(), "a")

        self._user_stats = json.loads(user_stats)
        return int(self._user_stats["userMetrics"][0]["totalActivities"])

    def list(self, count, offset=0):
        total_downloaded = 0
        ret = []

        # This while loop will download data from the server in multiple chunks, if necessary.
        while total_downloaded < count:
            # Maximum chunk size 'limit_maximum' ... 400 return status if over maximum.  So download
            # maximum or whatever remains if less than maximum.
            # As of 2018-03-06 I get return status 500 if over maximum
            num_to_download = min(self.LIMIT_MAXIMUM, count - total_downloaded)

            search_params = {"start": offset + total_downloaded, "limit": num_to_download}

            # Query Garmin Connect
            activity_list = self.http_req(self.URL_GC_LIST + urllib.parse.urlencode(search_params))
            lst = json.loads(activity_list)
            total_downloaded += len(lst)
            # write_to_file(ARGS.directory + "/activity_list.json", ACTIVITY_LIST.decode(), "a")
            ret.extend(lst)
        return ret

    def download(self, activity_id):
        activity_id = str(activity_id)
        logger.info("Downloading activity '%s'", activity_id)
        downloader = self._DOWNLOADERS[self.format](self, activity_id)
        downloader.download()

    def process(self, count=0):
        lst = self.list(count if count else self.count())
        # Process each activity.
        for a in lst:
            # Display which entry we're working on.
            activity_id = str(a["activityId"])
            logger.detail("Garmin Connect activity: [%s] %s", activity_id, a["activityName"])
            q = GCActivity.query('"activity_id" =', activity_id, parent=self.user)
            activity = q.get()
            if not activity:
                self.download(activity_id)
                activity = GCActivity(parent=self.user)
                activity.activity_id = activity_id
                activity.put()


if __name__ == "__main__":
    def main():
        uid = sys.argv[1]
        password = sys.argv[2]
        user = None
        with gripe.db.Tx.begin():
            u = grizzle.UserManager().get(uid)
            if u.authenticate(password=password):
                user = u
        if not user:
            print("Authentication error", file=sys.stderr)
            return 1

        try:
            gc = GarminConnect(user)
            gc.credentials(gripe.Config.garmin.connect.username, gripe.Config.garmin.connect.password)
            gc.login()
            lst = gc.list(10)
            for a in lst:
                print(a)
            return 0
        except Exception:
            traceback.print_exc()
            return 1

    sys.exit(main())
