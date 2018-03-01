#
# Copyright (c) 2017 Jan de Visser (jan@sweattrails.com)
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

import webapp2

import gripe
import grit.handlers
import sweattrails.session
import sweattrails.web.upload

logger = gripe.get_logger(__name__)


class SessionHandler(grit.handlers.PageHandler):
    def prepare_query(self, q):
        q['"athlete" = '] = str(self.user.key())
        return q

    def _get_run_context(self, ctx):
        activity = ctx["object"]
        q = sweattrails.session.RunPace.query(parent=activity.intervalpart, _sortorder="distance",
                                              include_subclasses=False, keys_only=False)
        ctx["paces"] = [p for p in filter(lambda obj: isinstance(obj, dict) or obj.can_read(), q)]

    def _get_bike_context(self, ctx):
        pass

    def _get_swim_context(self, ctx):
        pass

    def get_context(self, ctx):
        super(SessionHandler, self).get_context(ctx)
        activity = ctx["object"]
        if activity:
            q = sweattrails.session.Interval.query(parent=activity, _sortorder="timestamp",
                                                   include_subclasses=False, keys_only=False)
            intervals = [o for o in filter(lambda obj: isinstance(obj, dict) or obj.can_read(), q)]
            if len(intervals):
                ctx["intervals"] = intervals
            ctx["has_heartrate"] = activity.max_heartrate > 0
            ctx["has_power"] = hasattr(activity.intervalpart, "max_power") and (activity.intervalpart.max_power > 0)
            ctx["has_cadence"] = hasattr(activity.intervalpart, "max_cadence") and \
                (activity.intervalpart.max_cadence > 0)
            if isinstance(activity.intervalpart, sweattrails.session.RunPart):
                ctx["activitytype"] = "run"
                self._get_run_context(ctx)
            elif isinstance(activity.intervalpart, sweattrails.session.BikePart):
                ctx["activitytype"] = "bike"
                self._get_bike_context(ctx)
            elif isinstance(activity.intervalpart, sweattrails.session.SwimPart):
                ctx["activitytype"] = "swim"
                self._get_swim_context(ctx)
        return ctx


app = webapp2.WSGIApplication([
    webapp2.Route(
        r'/st/activities',
        handler="grit.handlers.PageHandler", name='list-activities',
        defaults={
            "kind": sweattrails.session.Session
        }),
    webapp2.Route(
        r'/st/activity/<key>',
        handler="sweattrails.web.handlers.SessionHandler", name='manage-activity',
        defaults={
            "kind": sweattrails.session.Session
        }
    ),
    webapp2.Route(
        r'/st/upload',
        handler="grit.upload.Uploader", name='upload-activities',
        defaults={
            "action": sweattrails.web.upload.UploadActivity,
            "param": "activity"
        })
    ], debug = True)
