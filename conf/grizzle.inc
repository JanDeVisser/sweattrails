{
    "author": "Jan de Visser",
    "version": "1.0",
    "usermanager": "grizzle.UserManager",
    "mounts": [
        { "path": "/profile", "app": "grizzle.app", "roles": [ "user" ]},
        { "path": "/users/?|/users/.+", "app": "grizzle.app", "roles": [ "admin" ]}
    ],
    "grizzle": {
        "root": "root@sweattrails.com",
        "oauth": {
            "browser": "grizzle.qt.OAuthBrowser"
        }
    }
}
