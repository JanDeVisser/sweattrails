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


import random
import sys
import uuid

import gripe.role

logger = gripe.get_logger("gripe")

#############################################################################
# E X C E P T I O N S
#############################################################################


class UserExists(gripe.AuthException):
    def __init__(self, uid):
        self._uid = uid
        logger.debug(str(self))

    def __str__(self):
        return "User with ID %s already exists" % self._uid


class UserDoesntExists(gripe.AuthException):
    def __init__(self, uid):
        self._uid = uid
        logger.debug(str(self))

    def __str__(self):
        return "User with ID %s doesn't exists" % self._uid


class InvalidConfirmationCode(gripe.AuthException):
    def __init__(self):
        logger.debug(str(self))

    def __str__(self):
        return "Invalid user confirmation code"


class BadPassword(gripe.AuthException):
    def __init__(self, uid):
        self._uid = uid
        logger.debug(str(self))

    def __str__(self):
        return "Bad password for user with ID %s" % self._uid


class GroupExists(gripe.AuthException):
    def __init__(self, gid):
        self._gid = gid
        logger.debug(str(self))

    def __str__(self):
        return "Group with ID %s already exists" % self._gid


class GroupDoesntExists(gripe.AuthException):
    def __init__(self, gid):
        self._gid = gid
        logger.debug(str(self))

    def __str__(self):
        return "Group with ID %s doesn't exists" % self._gid


#############################################################################
# A B S T R A C T  C L A S S E S
#############################################################################


class AbstractAuthObject(gripe.role.Principal):
    def role_objects(self, include_self = True):
        s = set()
        for rname in self.roles(explicit = True):
            role = gripe.role.Guard.get_rolemanager().get(rname)
            if role:
                s |= role.role_objects()
            else:
                logger.warn("Undefined role %s mentioned in '%s'.has_roles", rname, self.email)
        return s


@gripe.abstract("gid")
class AbstractUserGroup(AbstractAuthObject):
    def authenticate(self, **kwargs):
        return False

@gripe.abstract("groupnames")
@gripe.abstract("uid")
@gripe.abstract("displayname")
@gripe.abstract("confirm")
@gripe.abstract("changepwd")
class AbstractUser(AbstractAuthObject):
    def role_objects(self, include_self = True):
        s = super(AbstractUser, self).role_objects()
        for g in self.groups():
            s |= g.role_objects()
        return s

    def groups(self):
        ret = set()
        for gid in self.groupnames():
            group = gripe.role.Guard.get_groupmanager().get(gid)
            if group:
                ret.add()
        return ret

    def logged_in(self, session):
        pass

    def logged_out(self, session):
        pass


def generate_password():
    return "".join(random.sample("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ01234567890.,!@#$%^&*()_+=-", 10))


if __name__ == "__main__":
    pass
