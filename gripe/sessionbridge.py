# To change this template, choose Tools | Templates
# and open the template in the editor.

__author__="jan"
__date__ ="$16-Feb-2013 10:00:26 PM$"

import gripe

class SessionBridge(object):
    def __init__(self, userid, roles):
        self._userid = userid
        self._roles = roles

    def userid(self):
        return self._userid

    def roles(self):
        return self._roles

_sessionbridge = None

def set_sessionbridge(bridge):
    global _sessionbridge
    _sessionbridge = bridge

def get_sessionbridge():
    global _sessionbridge
    if not _sessionbridge:
        _sessionbridge = SessionBridge(gripe.Config.gripe.defaultuser, gripe.Config.gripe.defaultroles)
    return _sessionbridge

