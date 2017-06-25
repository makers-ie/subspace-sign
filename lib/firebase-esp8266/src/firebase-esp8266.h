#ifndef FIREBASE_ESP8266_H_
#define FIREBASE_ESP8266_H_

#include "firebase-common.h"

/* --- Types --- */
struct FirebaseAppOptions {
	char *project_id;
};

struct FirebaseApp {
};

struct FirebaseAuth {
};

struct FirebaseDb {
};

struct FirebaseRef {
};

struct FirebaseSnapshot {
};

struct FirebaseUser {
};

/* --- Functions --- */
extern int firebase_app_init(struct FirebaseApp *app, const struct FirebaseAppOptions *opts);
extern void firebase_app_clean(struct FirebaseApp *app);
extern int firebase_app_get_auth(struct FirebaseApp *app, struct FirebaseAuth *out);
extern int firebase_app_get_db(struct FirebaseApp *app, struct FirebaseDb *out);

extern int firebase_auth_clean(struct FirebaseAuth *auth);
extern int firebase_auth_sign_in_anonymously(struct FirebaseAuth *auth, const struct FirebaseUser *user);

extern int firebase_db_clean(struct FirebaseDb *db);
extern int firebase_db_get_ref(struct FirebaseDb *db, const char *path, struct FirebaseRef *out);
extern void firebase_db_set_online(struct FirebaseDb *db, bool v);

extern void firebase_ref_clean(struct FirebaseRef *ref);
extern int firebase_ref_get_value(struct FirebaseRef *ref, struct FirebaseSnapshot *out);

extern void firebase_snapshot_clean(struct FirebaseSnapshot *snap);
extern int firebase_snapshot_chdir(struct FirebaseSnapshot *snap, const char *path);
extern fb_ssize_t firebase_snapshot_get_string(struct FirebaseSnapshot *snap, char *buf, fb_ssize_t size);
extern int firebase_snapshot_set_string(struct FirebaseSnapshot *snap, const char *str, fb_ssize_t len);

extern int firebase_user_clean(struct FirebaseUser *user);
extern bool firebase_is_anonymous(struct FirebaseUser *user);

#endif /* FIREBASE_ESP8266_H_ */
