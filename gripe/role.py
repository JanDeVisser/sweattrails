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


import gripe
import gripe.managedobject
import gripe.url

logger = gripe.get_logger("gripe")


class RoleExists(gripe.managedobject.ObjectExists):
    def __init__(self, cls, role):
        super(RoleExists, self).__init__(cls, role)


class RoleDoesntExist(gripe.AuthException):
    def __init__(self, role):
        self._role = role
        logger.debug(str(self))

    def __str__(self):
        return "Role with ID %s does not exists" % self._role


class Guard(object):
    _managers = { }

    @classmethod
    def _get_manager(cls, manager, default):
        if manager not in cls._managers:
            logger.debug("Instantiating %s manager - default %s", manager, default)
            cls._managers[manager] = gripe.Config.resolve("app.%smanager" % manager, default)()
        return cls._managers[manager]

    @classmethod
    def get_usermanager(cls):
        return cls._get_manager("user", "gripe.UserManager")

    @classmethod
    def get_groupmanager(cls):
        return cls._get_manager("group", "gripe.UserGroupManager")

    @classmethod
    def get_rolemanager(cls):
        return cls._get_manager("role", "gripe.RoleManager")

    @classmethod
    def get_sessionmanager(cls):
        return cls._get_manager("session", "grit.SessionManager")

    @classmethod
    def get_guard(cls):
        return cls._get_manager("guard", "grit.Session")


@gripe.abstract(("role_objects",
                 """
                    Returns a collection (list or set) of all role object 
                    assigned to the object. either directly or by inheritance.
                    It is recommended that the role objects are instances of
                    a class extending the AbstractRole class.
                    
                    Returns:
                        A set or list of role objects, so not role names. 
                 """),
                 ("authenticate",
                  """
                    Authenticates the principal.
                    
                    Args:
                        **kwargs: name-value pair of data needed to 
                          authenticate the principal.
                          
                    Returns:
                        True if the authentication succeeds, False
                        otherwise.
                  """))
class Principal(object):
    """
        Abstract base class for objects that have roles. In traditional
        AAA systems these are roles, groups, and users: A role can be
        assigned to other roles, i.e. an admin is a user, so the admin role
        has the user role assigned to it. In addition, roles can be assigned
        to groups and users as well.
        
        The exact role resolution depends on whether e.g. groups can be nested
        or not, and is therefore left to the actual implementations of the
        gripe.auth subsystem.
        
        Implementations of this class should implement the role assignment
        protocol. This means that the following should provided:
        * Either a _roles instance attribute as a set or list containing the 
          roles assigned to the object, or
        * An _add_role(role) and an _explicit_roles() method to assign and
          retrieve role assignments. _add_role(role) should take a role name 
          string and the return value is ignored. _explicit_roles() should
          return a set of role names.
          
        Additionally, a role_objects() method should be implemented. This
        method should return a collection (list or set) of all role object 
        assigned to the object. either directly or by inheritance. Note that
        this method should return role objects, not role names. 
    """

    def roles(self, explicit = False):
        """
            Returns the names of the roles self has in a set.
            
            Args:
                explicit: If False, the roles self inherits from 
                    the roles it belongs to are included in the 
                    result as well. If True, only the roles explicitely
                    (directly) assigned to self are returned.
                    
            Returns:
                A set containing the names of the roles assigned to 
                self. This set can be empty.
                
            Raises:
                AssertionError: If implementing class does not follow the
                    role assignment protocol by not providing either an
                    _explicit_roles method or _roles attribute.
        """
        if not explicit:
            return {role.rolename() for role in self.role_objects()}
        else:
            if hasattr(self, "_explicit_roles") and callable(self._explicit_roles):
                return self._explicit_roles()
            elif hasattr(self, "_roles"):
                return self._roles if self._roles else set()
            else:
                assert 0, "Class %s must implement either _explicit_roles() or provide a _roles attribute" % self.__class__.__name__

    def add_role(self, role):
        """
            Assigns the provided role to self. The role can be provided as a 
            string or as an AbstractRole object. If the role is provided as a
            string and no role with that name exists, the current role 
            assignments are not changed.
            
            This method uses the role assignment protocol described above, i.e.
            the _add_role(str) method is called, or, if that method is not 
            available, the role will be added to the _roles attribute which
            is assumed to be a set of strings. 
            
            Args:
                role: The role to assign to this object. This can be a role
                object or a role name string.
        """
        if isinstance(role, AbstractRole):
            r = role
            role = r.rolename()
        else:
            role = str(role)
            r = Guard.get_rolemanager().get(role)
        if r:
            if hasattr(self, "_add_role") and callable(self._add_role):
                return self._add_role(role)
            elif hasattr(self, "_roles"):
                return self._roles.add(role)
            else:
                assert 0, "Class %s must implement either _add_role() or provide a _roles attribute" % self.__class__.__name__

    def urls(self, urls = None):
        if not hasattr(self, "_urls"):
            self._urls = gripe.url.UrlCollection(self)
        if urls is not None:
            self._urls.clear()
            if isinstance(urls, (list, tuple, set)):
                self._urls.append(urls)
            elif isinstance(urls, gripe.url.UrlCollection):
                self._urls.copy(urls)
            elif isinstance(urls, dict):
                if urls.get("urls") is not None:
                    self._urls.append(urls["urls"])
            else:
                assert 0, "[%s]%s: Cannot initialize urls with %s" % (self.__class__.__name__, self, urls)
            logger.debug("%s._urls = %s (From %s)", self, self._urls, urls)
        ret = gripe.url.UrlCollection(self._urls)
        for role in self.role_objects(False):
            ret.append(role._urls)
        logger.debug("%s.urls() = %s", self, ret)
        return ret

    def has_role(self, roles):
        """
            Determines if self has one or more roles listed in the roles
            parameter. 
            
            This method uses the roles(True) method above, so depends on the
            role resolution implemented in the inheriting class, and therefore
            will include inherited roles, if implemented. 
            
            Args:
                roles: Either be a string denoting a single role
                    name or an iterable of role names.
                    
            Returns:
                True if self has one or more of the indicated roles, False
                otherwise.
        """
        if isinstance(roles, str):
            roles = {roles}
        myroles = self.roles()
        logger.info("has_role: %s & %s = %s", set(roles), myroles, set(roles) & myroles)
        return len(set(roles) & myroles) > 0


class ManagedPrincipal(Principal, gripe.managedobject.ManagedObject):
    def __initialize__(self, **principaldef):
        self._roles = set(principaldef.pop("has_roles") if "has_roles" in principaldef else []) 
        self.urls(principaldef.pop("urls") if "urls" in principaldef else {})
        return principaldef
    

@gripe.abstract("rolename")
class AbstractRole(Principal):

    def role_objects(self, include_self=True):
        roles = [self]
        ret = {self} if include_self else set()
        while roles:
            role = roles.pop()
            for rname in role.roles(explicit=True):
                has_role = Guard.get_rolemanager().get(rname)
                if has_role and has_role not in ret:
                    ret.add(has_role)
                    roles.append(has_role)
        return ret


@gripe.managedobject.objectexists(RoleExists)
class Role(AbstractRole, ManagedPrincipal):
    def rolename(self):
        """
            Implementation of AbstractRole.rolename(). Returns the role attribute.
        """
        return self.objid()
    
    def authenticate(self, **kwargs):
        return False


@gripe.abstract("get", "add")
class AbstractRoleManager(object):
    pass
