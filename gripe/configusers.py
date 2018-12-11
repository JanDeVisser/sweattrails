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


import sys

import gripe.auth
import gripe.managedobject
import gripe.role

logger = gripe.get_logger("gripe")

@gripe.managedobject.objectexists(GroupExists)
@gripe.managedobject.configtag("users")
class UserGroup(AbstractUserGroup, gripe.role.ManagedPrincipal):
    def gid(self):
        return self.objid()

@gripe.managedobject.objectexists(UserExists)
@gripe.managedobject.idattr("email")
@gripe.managedobject.labelattr("display_name")
@gripe.managedobject.configtag("users")
class User(AbstractUser, gripe.role.ManagedPrincipal):
    def __initialize__(self, **user):
        self._groups = user.pop("has_groups") if "has_groups" in user else []
        self._groups = set(self._groups)
        user = super(User, self).__initialize__(**user)
        return user

    def uid(self):
        return self.objid()

    def displayname(self):
        return self.objectlabel()

    def groupnames(self):
        return self._groups

    def authenticate(self, **kwargs):
        password = kwargs.get("password")
        logger.debug("User(%s).authenticate(%s)", self, password)
        return self.password == password

    def confirm(self, status = 'Active'):
        logger.debug("User(%s).confirm(%s)", self, status)
        self.status = status
        self.put()

    def changepwd(self, oldpassword, newpassword):
        logger.debug("User(%s).authenticate(%s)", self, oldpassword, newpassword)
        self.password = newpassword
        self.put()


if __name__ == "__main__":
    pass
