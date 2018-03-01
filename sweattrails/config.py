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

import gripe
import gripe.db
import grizzle
import grumble.model
import grumble.image
import grumble.meta
import grumble.property

logger = gripe.get_logger(__name__)


class FlagProperty(grumble.property.StringProperty):
    def getvalue(self, instance):
        code = instance.countrycode
        return "http://flagspot.net/images/{0}/{1}.gif".format(code[0:1].lower(), code.lower()) \
            if code else None

    def setvalue(self, instance, value):
        pass


class Country(grumble.model.Model):
    countryname = grumble.property.StringProperty(verbose_name="Country name", is_label=True)
    countrycode = grumble.property.StringProperty(verbose_name="ISO 3166-1 code", is_key=True)
    flag_url = FlagProperty(transient=True)

#
# ============================================================================
#  A C T I V I T Y  P R O F I L E  A N D  P A R T S
# ============================================================================
#

# ----------------------------------------------------------------------------
#  PROFILE COMPONENTS
# ----------------------------------------------------------------------------


class ProfileReference(object):
    @classmethod
    def get_node_definition(cls):
        return NodeTypeRegistry.get_by_ref_class(cls)


@grumble.model.cached
class SessionType(grumble.model.Model, ProfileReference):
    _resolved_parts = set()
    name = grumble.property.StringProperty(verbose_name="Activity", is_key=True, scoped=True)
    description = grumble.property.StringProperty()
    intervalpart = grumble.property.StringProperty()
    trackDistance = grumble.property.BooleanProperty()
    speedPace = grumble.property.StringProperty(choices={'Speed', 'Pace', 'Swim Pace'})
    icon = grumble.property.StringProperty()

    def get_interval_part_type(self, profile):
        node = profile.get_node(SessionType, self.name)
        part = node.get_root_property("intervalpart")
        if part:
            if part not in self._resolved_parts:
                gripe.resolve(part)
                self._resolved_parts.add(part)
            return grumble.Model.for_name(part)
        else:
            return None


class GearType(grumble.model.Model, ProfileReference):
    name = grumble.property.StringProperty(is_key=True, scoped=True)
    description = grumble.property.StringProperty()
    icon = grumble.image.ImageProperty()
    partOf = grumble.SelfReferenceProperty(collection_verbose_name="Parts", verbose_name="Part of")
    usedFor = grumble.ReferenceProperty(SessionType, verbose_name="Used for")


class CriticalPowerInterval(grumble.model.Model, ProfileReference):
    name = grumble.property.StringProperty(is_key=True, scoped=True)
    duration = grumble.property.TimeDeltaProperty(verbose_name="Duration")


class CriticalPace(grumble.model.Model, ProfileReference):
    name = grumble.property.StringProperty(is_key=True, scoped=True)
    distance = grumble.property.IntegerProperty(verbose_name="Distance")  # In m


class Zone(grumble.model.Model, ProfileReference):
    name = grumble.property.StringProperty(is_key=True, scoped=True)


class PaceZone(Zone):
    sortorder = "minSpeed"
    sortdirection = False
    minSpeed = grumble.property.IntProperty()


class PowerZone(Zone):
    sortorder = "minPower"
    sortdirection = False
    minPower = grumble.property.IntProperty()


class HeartrateZone(Zone):
    sortorder = "minHeartrate"
    sortdirection = False
    minHeartrate = grumble.property.IntProperty()


# ----------------------------------------------------------------------------
#  NODE MACHINERY
# ----------------------------------------------------------------------------


class NodeTypeRegistry(object):
    _by_ref_name = {}
    _by_ref_class = {}
    _by_node_class = {}

    def __init__(self):
        pass

    @classmethod
    def register(cls, definition):
        cls._by_ref_name[definition._ref_name] = definition
        cls._by_ref_class[definition._ref_class] = definition
        cls._by_node_class[definition._node_class] = definition

    @classmethod
    def get_by_name(cls, ref_name):
        return cls._by_ref_name[ref_name]

    @classmethod
    def get_by_ref_class(cls, ref_class):
        return cls._by_ref_class[ref_class]

    @classmethod
    def get_by_node_class(cls, node_class):
        return cls._by_node_class[node_class]

    @classmethod
    def names(cls):
        return cls._by_ref_name.keys()

    @classmethod
    def types(cls):
        return cls._by_ref_name.values()


class NodeTypeDefinition(object):
    def __init__(self, ref_name, ref_class, node_class):
        self._ref_name = ref_name
        self._ref_class = ref_class
        self._node_class = node_class
        NodeTypeRegistry.register(self)

    def name(self):
        return self._ref_name

    def ref_class(self):
        return self._ref_class

    def node_class(self):
        return self._node_class

    def name_property(self):
        return self.ref_class().keyproperty()

    def link_properties(self):
        if not hasattr(self, "_link_props"):
            self._link_props = {}
            for (p, prop) in self.node_class().properties().items():
                if isinstance(prop, grumble.ReferenceProperty) and issubclass(prop.reference_class, NodeBase):
                    self._link_props[p] = NodeTypeRegistry.get_by_node_class(prop.reference_class)
        return self._link_props

    def get_reference_by_name(self, profile, key_name):
        p = profile.parent()
        ref = self.ref_class().get_by_key_and_parent(key_name, p)
        if ref is None and p is not None:
            ref = self.ref_class().get_by_key_and_parent(key_name, None)
        assert ref, "Cannot find reference to %s:%s" % (self.ref_class(), key_name)
        return ref

    def get_all_linked_references(self, profile):
        q = self.node_class().query(ancestor=profile)
        if hasattr(self.node_class(), "sortorder"):
            q.add_sort(self.node_class().sortorder, getattr(self.node_class(), "sortdirection", True))
        return [getattr(node, self.name()) for node in q]

    def get_or_create_node(self, profile, descriptor, parent=None):
        ref = self.ref_class().get(descriptor[self.name()]) \
            if self.name() in descriptor \
            else self.get_reference_by_name(profile, descriptor[self.name_property()])
        if not ref:
            ref = self.ref_class()(parent=profile.parent())
        assert ref, "No reference found for %s in %s" % (self.name(), descriptor)
        node = self.get_or_create_node_for_reference(profile, ref, parent)
        self.update_node(node, descriptor)
        return node

    def update_node(self, node, descriptor):
        ref = getattr(node, self.name())
        assert ref, "%s.update_node: no reference" % self.name()
        profile = node.get_profile()
        if ref.parent_key() == profile.parent_key():
            ref.update(descriptor)
        dirty = False
        d = dict(descriptor)
        for (prop, t) in self.link_properties().items():
            if prop in d:
                descr = d[prop]
                if descr is not None:
                    n = t.get_or_create_node(profile, d[prop])
                    setattr(node, prop, n)
                    dirty = True
                del d[prop]
        if dirty:
            node.put()
        l = []
        for p in d:
            if p not in node.properties():
                l.append(p)
        for p in l:
            del d[p]
        if d:
            node.update(d)
        return node

    def get_or_create_node_for_reference(self, profile, ref, parent=None):
        node = self.get_node_for_reference(profile, ref)
        if not node:
            node = self.node_class()(parent=parent if parent else profile)
            setattr(node, self.name(), ref)
            node.put()
        assert node, "%s.get_or_create_node_for_reference(): No node" % self.name()
        return node

    def get_node_for_reference(self, profile, ref):
        q = self.node_class().query(ancestor=profile)
        q.add_filter(self.name(), "=", ref)
        return q.get()

    def get_node_by_reference_name(self, profile, key_name):
        ref = self.get_reference_by_name(profile, key_name)
        return self.get_node_for_reference(profile, ref)

    def get_or_duplicate_reference(self, original, profile):
        # Only copy if reference is owned by another profile. If the reference
        # is not owned by another profile, it's a global entity.
        ref_key = getattr(original, self.name_property())
        if original.parent():
            return original
        # Check if the reference already exists:
        ref = self.get_reference_by_name(profile, ref_key)
        if not ref:
            ref = self.ref_class().create(original.to_dict(), profile.parent())
            ref.put()
        return ref

    def get_or_duplicate_node(self, original, profile):
        orig_ref = getattr(original, self.name())
        ref_key = getattr(orig_ref, self.name_property())
        node = self.get_node_by_reference_name(
                profile, ref_key)
        return node if node else self.duplicate_node(original, profile)

    def duplicate_node(self, original, profile):
        orig_ref = getattr(original, self.name())
        ref = self.get_or_duplicate_reference(orig_ref, profile)
        if original.samekind(original.parent()):
            parent = self.get_or_duplicate_node(original.parent(), profile)
        else:
            parent = profile
        node = self.get_or_create_node_for_reference(profile, ref, parent)
        dirty = False
        for (prop, t) in self.link_properties().items():
            link = getattr(original, prop)
            if link:
                n = t.get_or_duplicate_node(link, profile)
                setattr(node, prop, n)
                dirty = True
        for (pname, prop) in node.properties().items():
            if not isinstance(prop, grumble.reference.ReferenceProperty):
                setattr(node, pname, getattr(original, pname))
                dirty = True
        if dirty:
            node.put()
        return node


@grumble.model.abstract
class NodeBase(grumble.model.Model):

    @classmethod
    def get_node_definition(cls):
        return NodeTypeRegistry.get_by_node_class(cls)

    def get_reference(self):
        return getattr(self, self.get_node_definition().name())

    @classmethod
    def is_tree(cls):
        return False

    def scope(self):
        if not hasattr(self, "_scope"):
            r = self.root()
            self._scope = None if r.kind() == ActivityProfile.kind() else r
        return self._scope

    def get_profile(self):
        if not hasattr(self, "_profile"):
            path = self.pathlist()
            # FIXME Ugly -- sort of fixed it but still ugly
            # Should registered parts inject a 'get_bla_part' method
            # in the User class? Would require new metaclass...
            self._profile = path[0] \
                if grumble.meta.Registry.get("activityprofile").samekind(path[0]) \
                else path[1]
        return self._profile

    def sub_to_dict(self, d, **flags):
        r = d.pop(self.get_node_definition().name())
        r["refkey"] = r["key"]
        del r["key"]
        d.update(r)
        return d


@grumble.model.abstract
class TreeNodeBase(NodeBase):
    @classmethod
    def is_tree(cls):
        return True
    
    def get_root_property(self, prop):
        ref = self.get_reference()
        if ref:
            ref = ref()
            val = getattr(ref, prop)
            if val is not None:
                return val
        p = self.parent()
        if p is None:
            return None
        return p.get_root_property(prop) if hasattr(p, "get_root_property") else None

    def get_subtypes(self, all=False):
        q = self.children() if not all else self.descendents()
        return q.fetchall()

    def get_all_subtypes(self):
        return self.get_subtypes(True)

    def has_subtype(self, typ, deep=True):
        q = self.children() if not deep else self.descendents()
        q.add_filter(self.name(), '=', typ)
        return q.has()

    def on_delete(self):
        # FIXME: Go over link_properties and set reverse links to None
        p = self.get_profile()
        for sub in self.get_all_subtypes():
            sub.set_parent(p)
            sub.put()


# ----------------------------------------------------------------------------
#  PROFILE NODES
# ----------------------------------------------------------------------------

class SessionTypeNode(TreeNodeBase):
    sessionType = grumble.ReferenceProperty(SessionType)
    defaultfor = grumble.StringProperty()

    def intervalpart(self):
        return self.get_root_property("intervalpart")


class GearTypeNode(TreeNodeBase):
    gearType = grumble.ReferenceProperty(GearType)
    partOf = grumble.SelfReferenceProperty(collection_name="parts")
    usedFor = grumble.ReferenceProperty(SessionTypeNode)


class CriticalPowerIntervalNode(NodeBase):
    criticalPowerInterval = grumble.ReferenceProperty(CriticalPowerInterval, serialize=False)


class CriticalPaceNode(NodeBase):
    criticalPace = grumble.ReferenceProperty(CriticalPace)


class PaceZoneNode(NodeBase):
    paceZone = grumble.ReferenceProperty(PaceZone)


class PowerZoneNode(NodeBase):
    powerZone = grumble.ReferenceProperty(PowerZone)


class HeartrateZoneNode(NodeBase):
    heartrateZone = grumble.ReferenceProperty(HeartrateZone)

logger.debug("Config: %s", gripe.Config.app.sweattrails)


def setup_parts():
    for (part, partdef) in gripe.Config.app.sweattrails.activityprofileparts.items():
        NodeTypeDefinition(part, gripe.resolve(partdef.refClass), gripe.resolve(partdef.nodeClass))
setup_parts()


class ActivityProfile(grizzle.UserPart):
    name = grumble.StringProperty(is_key=True)
    description = grumble.StringProperty()
    isdefault = grumble.BooleanProperty()
    icon = grumble.image.ImageProperty()

    def sub_to_dict(self, descriptor, **flags):
        for part in grumble.Query(NodeBase, False, True).set_ancestor(self):
            node_type = NodeTypeRegistry.get_by_node_class(part.__class__)
            if node_type.name() not in descriptor:
                descriptor[node_type.name()] = []
            descriptor[node_type.name()].append(part.to_dict(**flags))
        return descriptor

    def sub_update(self, d):
        for ref_name in NodeTypeRegistry.names():
            if ref_name in d:
                self.update_or_create_nodes(d[ref_name], ref_name)
        return self

    def initialize(self):
        # Only do this for profiles that are user components:
        if not self.parent():
            return
        # Set the profile's name. Every user has only one profile object which
        # is manipulated whenever the user selects another template profile.
        self.name = "Activity Profile for " + self.root()().display_name

    def after_insert(self):
        # Find the default profile:
        profile = self.__class__.by("isdefault", True, parent=None)
        # If there is a default profile, import it into this profile:
        if profile:
            self.import_profile(profile)

    @classmethod
    def import_template_data(cls, data):
        """
            Import template activity profile data.
        """
        with gripe.db.Tx.begin():
            if cls.all(keys_only=True).count() > 0:
                return
        for profile in data:
            with gripe.db.Tx.begin():
                p = cls(name=profile.name, description=profile.description, isdefault=profile.isdefault)
                p.put()
                for ref_name in NodeTypeRegistry.names():
                    if ref_name in profile:
                        p.update_or_create_nodes(profile[ref_name], ref_name)

    def update_or_create_nodes(self, d, ref_name):
        node_type = NodeTypeRegistry.get_by_name(ref_name)
        d = d if isinstance(d, list) else [d]
        for subdict in d:
            is_a = subdict["isA"] if "isA" in subdict else None
            parent = node_type.get_or_create_node(self, is_a) if is_a else None
            if "key" in subdict:
                # Code path for JSON updates
                node = node_type.node_class().get(subdict["key"])
                if node:
                    if node.is_tree() and ((parent and parent != node.parent()) or (not parent and node.parent())):
                        node.set_parent(parent)
                        node.put()
                    node_type.update_node(node, subdict)
            else:
                # Path for import or JSON creates:
                # Find or create node, and update it.
                node_type.get_or_create_node(self, subdict, parent)

    def on_delete(self):
        pass

    # This deletes the node no questions asked. This means that all relations
    # get removed. Subtypes become root types, and parts become top-level
    # assemblies.
    # def delete_node(self, q, pointer_name, (node_class, pointer_class)):
    #    key = q[pointer_name] if pointer_name in q else None
    #    if key:
    #        node = node_class.get(key)
    #        if node:
    #            node.remove()
    #        return True
    #    else:
    #        return False

    def import_profile(self, profile):
        if type(profile) == str:
            profile = ActivityProfile.by("name", profile, parent=None)
        if not profile:
            return self
        if (profile.parent() == self.parent()) or not profile.parent():
            for node_type in NodeTypeRegistry.types():
                for node in node_type.node_class().query(ancestor=profile):
                    node_type.duplicate_node(node, self)
        return self

    def scope(self):
        if not hasattr(self, "_scope"):
            self._scope = self.parent()
        return self._scope

    def get_reference_from_descriptor(self, ref_class, d):
        name = None
        if type(d) == dict:
            key = d["key"] if "key" in d else None
            name = d["name"] if "name" in d else None
        else:
            key = str(d)
        assert key or name, "ActivityProfile.get_reference_from_descriptor: descriptor must specify name or key"
        if key:
            ref = ref_class.get(key)
            assert ref, "ActivityProfile.get_reference_from_descriptor(%s): " \
                "descriptor specified key = '%s' but no reference found" % (ref_class, key)
        else:
            return self.get_reference_by_name(ref_class, name)
        return ref

    def get_reference(self, ref_class, key_name):
        node_type = NodeTypeRegistry.get_by_ref_class(ref_class)
        return node_type.get_reference_by_name(self, key_name)

    def get_all_linked_references(self, ref_class):
        node_type = NodeTypeRegistry.get_by_ref_class(ref_class)
        return node_type.get_all_linked_references(self)

    def get_node(self, ref_class, key_name):
        node_type = NodeTypeRegistry.get_by_ref_class(ref_class)
        return node_type.get_node_by_reference_name(self, key_name)

    def get_default_SessionType(self, sport):
        q = SessionTypeNode.query(ancestor=self)
        q.add_filter("defaultfor", " = ", sport.lower())
        node = q.get()
        ret = node.sessionType if node else None
        return ret

    @classmethod
    def get_profile(cls, user):
        return user.get_part(cls)

#
# ============================================================================
#  B R A N D S  A N D  P R O D U C T S
# ============================================================================
#


class Brand(grumble.model.Model):
    name = grumble.property.StringProperty()
    description = grumble.property.StringProperty()
    logo = grumble.image.ImageProperty()
    website = grumble.property.StringProperty()  # FIXME LinkProperty
    about = grumble.property.TextProperty()
    country = grumble.reference.ReferenceProperty(Country)
    gearTypes = grumble.property.ListProperty()  # (grumble.Key) FIXME Make list items typesafe

    def sub_to_dict(self, descriptor, **flags):
        gts = []
        for gt in self.gearTypes:
            gts.append(GearType.get(gt).to_dict(**flags))
        descriptor["gearTypes"] = gts
        return descriptor


class Product(grumble.Model):
    name = grumble.StringProperty()
    icon = grumble.image.ImageProperty()
    isA = grumble.SelfReferenceProperty(collection_name="subtypes_set")
    usedFor = grumble.ReferenceProperty(SessionType)
    partOf = grumble.SelfReferenceProperty(collection_name="parts_set")
    baseType = grumble.SelfReferenceProperty(collection_name="descendenttypes")
