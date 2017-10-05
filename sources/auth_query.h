#ifndef OD_AUTH_QUERY_H
#define OD_AUTH_QUERY_H

/*
 * Odissey.
 *
 * Advanced PostgreSQL connection pooler.
*/

int od_auth_query(od_system_t*, od_schemeroute_t*,
                  shapito_parameter_t*,
                  shapito_password_t*);

#endif /* OD_AUTH_QUERY_H */
