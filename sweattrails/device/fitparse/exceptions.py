import sys

class FitError(Exception):
    def __init__(self, *args):
        super(FitError, self).__init__(*args)
        self.exc_info = sys.exc_info() if sys.exc_info() else (None, None, None)

class FitParseError(FitError):
    pass


class FitParseComplete(Exception):
    pass
