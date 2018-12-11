#
# Copyright (c) 2014 Jan de Visser (jan@sweattrails.com)
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

import sys
sys.path.insert(0, ".")

import json
import unittest
import webapp2

import grit


class ProfileTest(unittest.TestCase):

    def get_response(self, request, *expected):
        # print "Requesting %s %s" % (request.method, request.url)
        if self.cookie:
            request.headers['Cookie'] = self.cookie
        response = request.get_response(self.app)
        self.assertIn(response.status_int, expected,
                      msg="Expected one of %s, got %s for %s" % (expected, response.status, request.url))
        return response

    def request_json_data(self, location, **query):
        print "Getting JSON data from ", location
        print "JSON query", query
        request = webapp2.Request.blank(location)
        request.headers['ST-JSON-Request'] = json.dumps(query) if query else None
        response = self.get_response(request, 200)
        ret = json.loads(response.body)
        #print "Returned JSON data", json.dumps(ret, indent=2)
        self.assertIsNotNone(ret, msg="Expected JSON result set")
        self.assertIsNotNone(ret.get("data"), msg="Expected JSON result set data array")
        self.assertIsNotNone(ret.get("meta"), msg="Expected JSON result set metadata")
        return ret

    def post_json_data(self, location, data):
        print "Posting JSON data to ", location
        request = webapp2.Request.blank(location)
        request.method = "POST"
        request.headers['ST-JSON-Request'] = "true"
        request.body = json.dumps(data)
        response = self.get_response(request, 200)
        ret = json.loads(response.body)
        #print "Returned JSON data", json.dumps(ret, indent=2)
        self.assertIsNotNone(ret, msg="Expected JSON result set")
        # self.assertIsNotNone(ret.get("data"), msg="Expected JSON result set data array")
        # self.assertIsNotNone(ret.get("meta"), msg="Expected JSON result set metadata")
        return ret

    @classmethod
    def set_location(cls, location):
        cls.location = location

    @classmethod
    def set_cookie(cls, cookie):
        cls.cookie = cookie

    @classmethod
    def set_key(cls, key):
        cls.key = key

    @classmethod
    def setUpClass(cls):
        cls.app = grit.app
        cls.cookie = None

    def setUp(self):
        self.longMessage = True

    def test_01_get_landingpage(self):
        # print "Get landing page"
        self.get_response(webapp2.Request.blank('/'), 200)

    def test_02_request_login(self):
        # print "Requesting application login page"
        self.get_response(webapp2.Request.blank('/login'), 200)
        # print "Requested /login and got OK"

    def test_03_login(self):
        # print "Logging into application"
        request = webapp2.Request.blank("/login")
        request.method = "POST"
        request.POST["userid"] = "jan@de-visser.net"
        request.POST["password"] = "wbw417"
        request.POST["remember"] = "x"
        response = self.get_response(request, 200, 302)
        self.set_location(response.headers["Location"] if response.status_int == 302 else "/")
        cookie = response.headers["Set-Cookie"]
        parts = cookie.split(";")
        self.set_cookie(parts[0])
        # print "POSTed login data"

    def test_04_login_redirect(self):
        # print "Following login redirect to", self.location
        request = webapp2.Request.blank(self.location)
        request.headers['Cookie'] = self.cookie
        response = self.get_response(request, 200)
        # print "Login redirect OK"

    def test_05_get_profile_page(self):
        # print "Getting /profile"
        self.get_response(webapp2.Request.blank('/profile'), 200)
        # print "Requested /profile and got OK"

    def test_06_get_user_profile(self):
        users = self.request_json_data("/json/user")
        self.assertGreater(len(users), 0, msg="Expected at least one user")
        user = users["data"][0]
        self.set_key(user["key"])
        user_data = self.request_json_data("/json/user/%s" % self.key, _flags={"include_parts": True })
        user_data = user_data.get("data")
        print "Returned JSON data", json.dumps(user_data, indent=2)
        self.assertIsNotNone(user_data.get("_userprofile"), msg="Expected _userprofile part")

    def test_07_get_countries(self):
        countries = self.request_json_data("/json/country")
        self.assertGreater(len(countries), 0, msg="Expected at least one country")

    def test_08_update_user_profile(self):
        user_data = self.request_json_data("/json/user/%s" % self.key, _flags={"include_parts": True})
        user_data = user_data.get("data")
        updated_name = user_data["display_name"] + " (Updated)"
        user_data["display_name"] = updated_name
        user_data["_userprofile"]["dob"] = {"year": 1966, "month": 9, "day": 18}
        user_data["_userprofile"]["location"] = "(43.3, -80.3)"
        user_data["_userprofile"]["country"] = "CA"
        self.post_json_data("/json/user/%s" % self.key, user_data)

        user_data = self.request_json_data("/json/user/%s" % self.key, _flags={"include_parts": True})
        user_data = user_data.get("data")
        print "Userprofile after update:", json.dumps(user_data["_userprofile"], indent=2)
        self.assertEqual(user_data["display_name"], updated_name)
        self.assertEqual(user_data["_userprofile"]["country"], "CA")

if __name__ == '__main__':
    unittest.main()
