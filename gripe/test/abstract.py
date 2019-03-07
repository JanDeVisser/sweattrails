'''
Created on Feb 10, 2014

@author: jan
'''
import unittest

import gripe

@gripe.abstract("foo")
class A(object):
    def bar(self):
        print("bar")

@gripe.abstract("frob", "quux", "bar")
class B(A):
    def foo(self):
        print("foo")
        
    def frob(self):
        print("frob")
        

class TestAbstract(unittest.TestCase):


    def setUp(self):
        self.a = A()
        self.b = B()


    def tearDown(self):
        pass


    def testAFooAbstract(self):
        
        try:
            self.a.foo()
            assert 0, "a.foo() passed. That's wrong"
        except AssertionError:
            print("a.foo raised AssertionError")
            pass


    def testBPureVirtualQuux(self):             
        try:
            self.b.quux()
            assert 0, "b.quux() passed. That's wrong"
        except AssertionError:
            print("b.quux raised AssertionError")
            pass

    def testBShadowedFrob(self):             
        try:
            self.b.frob()
            assert 0, "b.frob() passed. That's wrong"
        except AssertionError:
            print("b.frob() raised AssertionError")
            pass

    def testBShadowedBar(self):             
        try:
            self.b.bar()
            assert 0, "b.bar() passed. That's wrong"
        except AssertionError:
            print("b.bar() raised AssertionError")
            pass

    def testBImplementedFoo(self):             
        try:
            self.b.foo()
            print("b.foo() passed. That's right")
        except AssertionError:
            print("b.foo() raised AssertionError")
            raise


if __name__ == "__main__":
    #imp sys;sys.argv = ['', 'Test.testName']
    unittest.main()