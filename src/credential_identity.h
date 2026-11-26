#ifndef GIN_CREDENTIAL_IDENTITY_H
#define GIN_CREDENTIAL_IDENTITY_H

/* Native bounded lexical policy, not authentication or normalization.
 * NULL is invalid. No token or display-name dependency. */
int gin_credential_identity_origin_valid(const char *origin);
int gin_credential_identity_subject_id_valid(const char *subject_uuid);

#endif
