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

import grizzle
import grumble.property
import grumble.reference


class Coach(grizzle.UserPart):
    def get_athletes(self):
        return [ap.get_user for ap in self.athleteparts]

class CoachedAthlete(grizzle.UserPart):
    coachpart = grumble.reference.ReferenceProperty(
        reference_class=Coach,
        collection_name="athleteparts")

    def get_coach(self):
        return self.coachpart.get_user()
