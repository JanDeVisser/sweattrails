#
# Copyright (c) 2013 Jan de Visser (jan@sweattrails.com)
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

import json


class ACL:
    def __init__(self, acl):
        self.set(acl)
        
    def set(self, acl):
        if isinstance(acl, dict):
            self._acl = dict(acl)
        elif isinstance(acl, str):
            self._acl = json.loads(acl)
        else:
            self._acl = {}
        for (role, perms) in list(self._acl.items()):
            assert role and role.lower() == role, \
                "ACL.set_acl: Role may not be None and and must be lower case"
            assert perms and perms.upper() == perms, \
                "ACL.set_acl: Permissions may not be None and must be upper case"
            
    def acl(self):
        if not hasattr(self, "_acl"):
            self.set(None)
        return self._acl
    
    def __call__(self):
        return self.acl()

    def set_ace(self, role, perms):
        assert role, "ACL.set_ace: Role must not be None"
        if not hasattr(self, "_acl"):
            self.set(None)
        perms = "".join(perms)
        self.acl()[role.lower()] = perms.upper() if perms else ""

    def get_ace(self, role):
        assert role, "ACL.set_ace: Role must not be None"
        if not hasattr(self, "_acl"):
            self.set(None)
        return set(self.acl().get(role.lower(), ""))

    def to_json(self):
        return json.dumps(self.acl())
