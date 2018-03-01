'''
Created on Aug 17, 2014

@author: jan
'''

import gripe

class FileImportError(gripe.Error):
    def __init__(self, message):
        self.message = str(message)
        
    def __str__(self):
        return "FileImportError: %s" % self.message

class SessionExistsError(FileImportError):
    def __init__(self, session):
        super(SessionExistsError, self).__init__("Session with start time %s already exists" % session.start_time)
        self.session = session
        
