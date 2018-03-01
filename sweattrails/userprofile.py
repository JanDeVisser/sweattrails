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

import grizzle
import grumble.property
import grumble.geopt


class UserProfile(grizzle.UserPart):
    country = grumble.property.StringProperty(default="CA")
    dob = grumble.property.DateProperty(verbose_name="Date of Birth")
    gender = grumble.property.StringProperty(choices=['male', 'female', 'other'], default='other')
    height = grumble.property.IntegerProperty(default=170)  # in cm
    units = grumble.property.StringProperty(choices=['metric', 'imperial'], default='metric')

    # 0 = Sunday, 6 = Saturday. Most people want to start on Monday
    weekstarts = grumble.property.IntegerProperty(minimum=0, maximum=6, default=1)

    location = grumble.geopt.GeoPtProperty()
    whoami = grumble.property.StringProperty(multiline=True)
    regkey = grumble.property.StringProperty()
    uploads = grumble.property.IntegerProperty(default=0)
    last_upload = grumble.property.DateTimeProperty()

    def after_insert(self):
        pass


class BikeProfile(grizzle.UserPart):
    def get_ftp(self, on_date=None):
        ftp = 0
        q = FTPHistory.query(parent=self).add_sort("snapshotdate")
        if on_date:
            q.add_filter("snapshotdate <= ", on_date)
        hentry = q.get()
        if hentry:
            ftp = hentry.ftp
        return ftp

    def set_ftp(self, ftp, on_date=None):
        hentry = FTPHistory(parent=self)
        hentry.ftp = ftp
        if on_date:
            hentry.snapshotdate = on_date
        hentry.put()

    def get_max_power(self, on_date=None):
        max_power = 0
        q = MaxPowerHistory.query(parent=self).add_sort("snapshotdate")
        if on_date:
            q.add_filter("snapshotdate <= ", on_date)
        hentry = q.get()
        if hentry:
            max_power = hentry.max_power
        return max_power

    def set_max_power(self, max_power, on_date=None):
        current = self.get_max_power(on_date)
        if current < max_power:
            hentry = MaxPowerHistory(parent=self)
            hentry.max_power = max_power
            if on_date:
                hentry.snapshotdate = on_date
            hentry.put()

    def get_watts_per_kg(self, watts, on_date):
        ret = 0
        weightpart = WeightMgmt.get_userpart(self.get_user())
        if weightpart is not None:
            weight = weightpart.get_weight(on_date)
            ret = watts/weight
        return ret


class FTPHistory(grumble.model.Model):
    snapshotdate = grumble.property.DateProperty(auto_now_add=True)
    ftp = grumble.property.IntegerProperty(default=0)  # FTP in Watts


class MaxPowerHistory(grumble.model.Model):
    snapshotdate = grumble.property.DateProperty(auto_now_add=True)
    max_power = grumble.property.IntegerProperty(default=0)  # Max power in Watts


class RunProfile(grizzle.UserPart):
    pass


class WeightMgmt(grizzle.UserPart):
    def get_weight(self, on_date=None):
        weight = None
        q = WeightHistory.query(parent=self).add_sort("snapshotdate")
        if on_date:
            q.add_filter("snapshotdate <= ", on_date)
        hentry = q.get()
        if hentry:
            weight = hentry.weight
        return weight


class WithingsAuth(grumble.model.Model):
    userid = grumble.property.StringProperty()
    public_key = grumble.property.StringProperty()


@grumble.property.transient
class BMIProperty(grumble.property.FloatProperty):
    def getvalue(self, instance):
        ret = self._get_storedvalue(instance)
        if not ret:
            user = instance.root()
            profile = user.get_part(UserProfile)
            h_m = float(profile.height) / 100
            ret = instance.weight / (h_m * h_m)
        return ret


class WeightHistory(grumble.model.Model):
    snapshotdate = grumble.property.DateProperty(auto_now_add=True)
    weight = grumble.property.FloatProperty(default=0.0)  # in kg
    bmi = BMIProperty()
    bfPercentage = grumble.property.FloatProperty(default=0.0)
    waist = grumble.property.FloatProperty(default=0.0)  # in cm


class CardioVascularHistory(grumble.model.Model):
    snapshotdate = grumble.property.DateProperty(auto_now_add=True)
    bpHigh = grumble.property.IntegerProperty(default=120, verbose_name="Systolic (high) Blood Pressure")
    bpLow = grumble.property.IntegerProperty(default=80, verbose_name="Diastolic (low) Blood Pressure")
    resting_hr = grumble.property.IntegerProperty(default=60, verbose_name="Resting Heartrate")


class WellnessDiary(grumble.model.Model):
    snapshotdate = grumble.property.DateProperty(auto_now_add=True)
    mood = grumble.property.IntegerProperty(minvalue=1, maxvalue=10)
    sleep_time = grumble.property.FloatProperty(default=0.0, verbose_name="Sleep Time")
    sleep_q = grumble.property.IntegerProperty(minvalue=1, maxvalue=10, verbose_name="Sleep Quality")
    health = grumble.property.TextProperty(multiline=True, verbose_name="Health Notes")


class SeizureMgmt(grizzle.UserPart):
    markers = grumble.property.ListProperty(verbose_name="Markers")
    triggers = grumble.property.ListProperty(verbose_name="Triggers")


class SeizureLog(grumble.model.Model):
    timestamp = grumble.property.DateTimeProperty(auto_now_add=True)
    description = grumble.property.TextProperty()
    severity = grumble.property.IntProperty()
    markers = grumble.property.JSONProperty()
    triggers = grumble.property.JSONProperty()
