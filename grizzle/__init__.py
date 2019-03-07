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

import enum

import gripe
import gripe.db
import gripe.smtp
import gripe.pgsql
import grumble.model
import grumble.property
import grumble.reference
import gripe.auth
import gripe.managedobject
import gripe.role
import gripe.url

logger = gripe.get_logger("grizzle")


@grumble.model.flat
class UserGroup(grumble.model.Model, gripe.auth.AbstractUserGroup):
    _flat = True
    group = grumble.property.TextProperty(is_key = True)
    description = grumble.property.TextProperty(is_label = True)
    has_roles = grumble.property.ListProperty()

    def gid(self):
        return self.group
    objid = gid

    def objectlabel(self):
        return self.description

    def _explicit_roles(self):
        return set(self.has_roles)


class GroupManager():
    def get(self, gid):
        ret = UserGroup.get_by_key(gid)
        return ret and ret.exists() and ret

    def add(self, gid, **attrs):
        logger.debug("grizzle.UserManager.add(%s, %s)", gid, attrs)
        g = self.get(gid)
        if g:
            logger.debug("grizzle.UserGroupManager.add(%s) Group exists", gid)
            raise gripe.auth.GroupExists(gid)
        else:
            attrs["group"] = gid
            if "label" in attrs:
                attrs["description"] = attrs["label"]
                del attrs["label"]
            g = UserGroup(**attrs)
            g.put()
            logger.debug("UserGroupManager.add(%s) OK", attrs)
            return g.gid()


@grumble.abstract
class UserPart(grumble.Model):
    def get_user(self):
        return self.parent()

    @classmethod
    def get_userpart(cls, user):
        return user.get_part(cls)

    def urls(self):
        return self._urls if hasattr(self, "_urls") else None


class UserPartKennel(UserPart):
    pass


class UserStatus(enum.Enum):
    """
        Banned - User is still there, content is still there, user cannot log in.
            Revertable.
        Inactive - At user's request, User is "deleted", content deleted, user
            cannot log in. Not revertable.
        Deleted - Admin "deleted" user, content deleted, user cannot log in.
            Not revertable.
    """
    Unconfirmed = 'Unconfirmed'
    Active = 'Active'
    Admin = 'Admin'
    Banned = 'Banned'
    Inactive = 'Inactive'
    Deleted = 'Deleted'


def onUserStatusAssign(user, oldstatus, newstatus):
    if newstatus in ["Inactive", "Deleted"]:
        user.deactivate()


GodList = ('jan@de-visser.net',)


@grumble.property.transient
class UserPartToggle(grumble.property.BooleanProperty):
    def __init__(self, partname, *args, **kwargs):
        super(UserPartToggle, self).__init__(*args, **kwargs)
        self.partname = partname

    def setvalue(self, instance, value):
        n = self.name.lower()
        p = instance.get_part(n)
        if (p is not None) and not value:
            instance._parts[n] = None
            for kennel in grumble.Query(UserPartKennel, keys_only=True, include_subclasses=True).set_parent(instance):
                p.set_parent(kennel)
                p.put()
        elif (p is None) and value and not instance.is_new():
            for kennel in grumble.Query(UserPartKennel, keys_only=True, include_subclasses=True).set_parent(instance):
                for p in grumble.Query(grumble.Model.for_name(self.partname), keys_only=True, include_subclasses=True) \
                        .set_parent(kennel):
                    p.set_parent(instance)
                    p.put()
            if p is None:
                p = grumble.Model.for_name(self.partname)(parent=instance)
                p.put()
            instance._parts[n] = p

    def getvalue(self, instance):
        return instance.get_part(self.name.lower()) is not None

_userpart_classes = {}
for pn in gripe.Config.key("app.grizzle.userparts"):
    _userpart_classes[pn.lower()] = gripe.resolve(pn)


def customize_user_class(cls):
    for (partname, partdef) in gripe.Config.key("app.grizzle.userparts").items():
        (_, _, name) = partname.rpartition(".")
        name = name.lower()
        partcls = _userpart_classes[partname.lower()]
        if "urls" in partdef and partdef.urls:
            partcls._urls = gripe.url.UrlCollection(name, partdef.label, 9, partdef.urls)
        if partdef.configurable:
            propdef = UserPartToggle(name, verbose_name=partdef.label, default=partdef.default)
            cls.add_property(name, propdef)


class User(grumble.Model, gripe.auth.AbstractUser, flat=True, customizer=staticmethod(customize_user_class)):
    email = grumble.property.TextProperty(is_key=True)
    password = grumble.property.PasswordProperty()
    status = grumble.property.TextProperty(
        choices=UserStatus,
        default='Unconfirmed',
        required=True,
        on_assign=onUserStatusAssign
    )
    display_name = grumble.property.TextProperty(is_label=True)
    has_roles = grumble.property.ListProperty(verbose_name="Roles",
                                              choices={r: role.get("label", r)
                                                       for r, role in gripe.Config.app["roles"].items()})

    def __init__(self, *args, **kwargs):
        self._parts_loaded = False
        grumble.Model.__init__(self, *args, **kwargs)

    def uid(self):
        return self.email
    objid = uid

    def displayname(self):
        return self.display_name
    objectlabel = displayname

    def groupnames(self):
        return { gfu.group for gfu in self.groupsforuser_set }

    def _explicit_roles(self):
        return set(self.has_roles)

    def authenticate(self, **kwargs):
        password = kwargs.get("password")
        return (self.exists() and
                self.is_active() and
                grumble.PasswordProperty.hash(password) == self.password)

    def confirm(self, status='Active'):
        logger.debug("User(%s).confirm(%s)", self, status)
        if self.exists():
            self.status = status
            if 'user' not in self.has_roles:
                self.has_roles.append('user')
            self.put()

    def changepwd(self, oldpassword, newpassword):
        logger.debug("User(%s).changepwd(%s, %s)", self, oldpassword, newpassword)
        if self.exists():
            self.password = newpassword
            self.put()

    def after_insert(self):
        kennel = UserPartKennel(parent=self)
        kennel.put()
        for (partname, partdef) in gripe.Config.key("app.grizzle.userparts").items():
            if partdef.default:
                part = _userpart_classes[partname.lower()](parent=self)
                part.put()

    def sub_to_dict(self, d, **flags):
        if "include_parts" in flags:
            for (k, part) in self._parts.items():
                if part:
                    d["_" + k] = part.to_dict(**flags)
        return d

    def on_update(self, d, **flags):
        self.load_parts()
        for (k, part) in self._parts.items():
            key = "_" + k
            if (key in d) and part:
                p = d[key]
                if isinstance(p, dict):
                    part.update(p, **flags)

    def get_part(self, part):
        with gripe.db.Tx.begin():
            self.load_parts()
            k = part.lower() if (isinstance(part, str)) else part.basekind().lower()
            return self._parts[k] if k in self._parts else None

    def after_load(self):
        self.load_parts()

    def init_parts(self):
        if not hasattr(self, "_parts"):
            self._parts = {}
            for part_name in gripe.Config.key("app.grizzle.userparts"):
                (_, _, name) = part_name.rpartition(".")
                self._parts[name.lower()] = None

    def load_parts(self):
        if not hasattr(self, "_parts"):
            self.init_parts()
            if not (self.is_new() or (hasattr(self, "_parts_loaded") and self._parts_loaded)):
                logger.debug("load_parts - _parts: %s", self._parts)
                for part in grumble.Query(UserPart, keys_only=False, include_subclasses=True).set_parent(self):
                    k = part.basekind().lower()
                    self._parts[k] = part
                    setattr(self, "_" + k, part)
                    logger.debug("part %s : %s", k, getattr(self, "_" + k))
                self._parts_loaded = True

    def urls(self, urls=None):
        if urls is not None:
            return super(User, self).urls(urls)
        else:
            ret = super(User, self).urls()
            for part in self._parts.values():
                if hasattr(part, "urls") and callable(part.urls):
                    u = part.urls()
                    if u:
                        ret.append(u)
            return ret

    def is_active(self):
        """
          An active user is a user currently in good standing.
        """
        return self.status == 'Active'

    def is_admin(self):
        return ("admin" in self.has_roles and self.is_active()) or (self.status == 'Admin') or self.is_god()

    def is_god(self):
        return self.uid() in GodList

    def deactivate(self):
        grumble.Query(UserPart, keys_only=False, include_subclasses=True).set_parent(self).delete()


@grumble.model.flat
class GroupsForUser(grumble.Model):
    _flat = True
    user = grumble.reference.ReferenceProperty(reference_class=User)
    group = grumble.reference.ReferenceProperty(reference_class=UserGroup)


class UserManager(object):

    def __init__(self):
        if "grizzle" not in gripe.Config:
            gripe.Config["grizzle"] = {}

    @classmethod
    def get(cls, userid):
        ret = User.get_by_key(userid)
        return ret if ret and ret.exists() else None

    def add(self, userid, **attrs):
        logger.debug("grizzle.UserManager.add(%s, %s)", userid, attrs)
        user = self.get(userid)
        if user:
            logger.debug("grizzle.UserManager.add(%s) User exists", userid)
            raise gripe.auth.UserExists(userid)
        else:
            attrs["email"] = userid
            user = User(**attrs)
            user.put()
            logger.debug("UserManager.add(%s) OK", attrs)
            return user

    def has_users(self):
        return User.all(keys_only=True).count() > 0

    def authenticate(self, **kwargs):
        uid = kwargs.get("uid")
        password = kwargs.get("password")
        if uid and password:
            user = self.get(uid)
            if user and user.authenticate(password=password):
                return user, password, kwargs.get("savecredentials", False)
            else:
                return None, None, None
        else:
            authenticator = kwargs.get("authenticator",
                                       gripe.Config.grizzle.get("authenticator", "grizzle.qt.authenticate"))
            authenticator = gripe.resolve(authenticator)
            return authenticator(**kwargs)


if False:
    import webapp2

    app = webapp2.WSGIApplication([
        webapp2.Route(
                r'/profile',
                handler="grizzle.profile.Profile",
                name='profile',
                defaults={
                    "kind": User
                }
        ),

        webapp2.Route(
                r'/users/<key>',
                handler="grit.handlers.PageHandler",
                name='manage-user',
                defaults={
                    "kind": User
                }
        ),

        webapp2.Route(
            r'/users',
            handler="grit.handlers.PageHandler", name='manage-users',
            defaults={
                "kind": User
            }
        )  # ,
    ], debug=True)
