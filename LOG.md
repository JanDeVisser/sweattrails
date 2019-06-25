
## 2019-05-02

Key refactoring seems OK. Endless loop in `grumpy.model.TreeModel._load` though. Parent not loaded.
Idea: if `parent` is not in `objects`, recursively load and handle right then and there instead of 
re-shoving the current `obj` into `todo`. This should work; recursion depth of tree you want to represent
in a TreeView should be limited.

## 2019-04-23

In `grumble.Model`, `key()` used `_key_scoped`. The semantics of that are, um, undefined. Should use the `parent_key()`
Refactoring `grumble.Key` to take a `None` or empty string parent key without complaining. This, of course, led to 
wholesale refactoring of the construction cycle of `grumble.Key`. Need to finish and maybe write a couple of tests.
