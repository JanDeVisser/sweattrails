#! /usr/bin/python

# To change this license header, choose License Headers in Project Properties.
# To change this template file, choose Tools | Templates
# and open the template in the editor.

__author__ = "jan"
__date__ = "$18-Sep-2013 8:57:43 AM$"

import gripe.db
import grumble

def check_age(instance, age):
    print "check_age", age
    if age < 0 or age >= 120:
        print "check_age: really?"
        raise Exception("A person cannot be %s years old" % age)
    else:
        print "check_age: OK"

class CanDriveProperty(grumble.BooleanProperty):
    transient = True

    def getvalue(self, instance):
        return instance.age >= 16

    def setvalue(self, instance, value):
        pass


class Person(grumble.Model):
    name = grumble.TextProperty(required = True, is_label = True,
        is_key = True, scoped = True)
    age = grumble.IntegerProperty(default = 30, validator = check_age)
    can_drive = CanDriveProperty()

print Person.can_drive.transient
print Person.schema()

def test():
    keys = []
    names = []
    with gripe.db.Tx.begin():
        print ">>> Creating Person object"
        jan = Person(name = "Jan", age = "470")
        assert jan.id() is None
        assert jan.name == "Jan"
        assert jan.age == 470
        assert jan.can_drive
        jan.put()
        assert jan.id() is not None, "jan.id() is still None after put()"
        x = jan.key()
        keys.append(x)
        names.append("Jan")

    with gripe.db.Tx.begin():
        print ">>> Retrieving Person object by key"
        y = Person.get(x)
        assert y.id() == x.id
        assert y.name == "Jan"
        assert y.age == 47
        y.age = 43
        y.put()
        assert y.age == 43

    with gripe.db.Tx.begin():
        print ">>> Creating Person with parent"
        tim = Person(name = "Tim", age = 9, parent = y)
        tim.put()
        assert tim.parent().id == y.id()
        keys.append(tim.key())
        names.append("Tim")

        print ">>> Querying all Person objects by Query"
        gripe.db.Tx.flush_cache()
        q = grumble.Query(Person)
        for p in q:
            assert p.key() in keys, "Key %s not known"

        print ">>> Querying all Person objects by Query, keys_only == False"
        gripe.db.Tx.flush_cache()
        q = grumble.Query(Person, False)
        for p in q:
            assert p.name in names, "Name %s not known"

        print ">>> Person.all()"
        assert Person.all().count() == 2
        for p in Person.all():
            assert p.name in names, "Name %s not known"

        print ">>> Person.count()"
        count = Person.count()
        assert count == 2

        print ">>> Person.get_by_key_and_parent()"
        gripe.db.Tx.flush_cache()
        tim2 = Person.get_by_key_and_parent("Tim", y)
        assert tim2, "Person.get_by_key_and_parent() returned None"
        assert tim2.name == "Tim", "Person.get_by_key_and_parent() returned %s" % tim2.name

        print ">>> Query with ancestor /"
        q = grumble.Query(Person)
        q.set_ancestor("/")
        for t in q:
            print t

        print ">>> Query with ancestor Jan"
        q = grumble.Query(Person)
        q.set_ancestor(y)
        for t in q:
            print t

        print ">>> Query with age = 9"
        q = grumble.Query(Person)
        q.add_filter("age", "=", 9)
        assert q.get().name == "Tim"

        print ">>> Query with name = Tim and age < 10"
        q = grumble.Query(Person)
        q.set_ancestor(y)
        q.add_filter("name", "=", 'Tim')
        q.add_filter("age", " < ", 10)
        assert q.get().name == "Tim"

        class Test2(grumble.Model):
            name = grumble.TextProperty(required = True, is_label = True)
            value = grumble.IntegerProperty(default = 12)
        mariska = Test2(name = "Mariska", value = 40, parent = y)
        mariska.put()

        class Test3(grumble.Model):
            name = grumble.TextProperty(required = True, is_label = True)
            value = grumble.IntegerProperty(default = 12)
        jeroen = Test3(name = "Jeroen", value = 44, parent = y)
        jeroen.put()

        print ">>> Person, Test2, Test3 with ancestor"
        q = grumble.Query((Person, Test2, Test3), False, ancestor = y)
        for t in q:
            print t.name


        print ">>> Test2, Test3 with ancestor"
        q = grumble.Query((Test2, Test3), False, ancestor = y)
        for t in q:
            print t.name

        print "<<<"

        print ">>> Subclassing models"
        class Test3Sub(Test3):
            lightswitch = grumble.BooleanProperty(default = False)

        t3s = Test3Sub(name = "T3S", value = "3", lightswitch = True)
        t3s.put()
        print t3s.name, t3s.value, t3s.lightswitch
        q = grumble.Query(Test3, False)
        for t in q:
            print t.name
        print "<<<"

        print ">>> Class with Reference property"
        class Pet(grumble.Model):
            name = grumble.TextProperty(required = True, is_key = True)
            owner = grumble.ReferenceProperty(Person)

        toffee = Pet(name = "Toffee", owner = jan)
        toffee.put()

        print ">>> Query for Reference property"
        q = grumble.Query(Pet)
        q.add_filter("owner", " = ", y)
        for t in q:
            print t

        y.pet_set.run()

        class SelfRefTest(grumble.Model):
            selfrefname = grumble.TextProperty(key = True, required = True)
            ref = grumble.SelfReferenceProperty(collection_name = "loves")

        luc = SelfRefTest(selfrefname = "Luc")
        luc.put()

        schapie = SelfRefTest(selfrefname = "Schapie", ref = luc)
        schapie.put()
        print schapie.to_dict()

        for s in luc.loves:
            print s

        luc.ref = schapie
        luc.put()
        print schapie.to_dict()
        print luc.to_dict()


if __name__ == '__main__':
    test()

