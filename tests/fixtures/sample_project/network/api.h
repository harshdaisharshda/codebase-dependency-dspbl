// no includes
// NOTE: deliberately shares the basename "api.h" with api/api.h — project-relative
// paths make these two distinct nodes (approved decision #5). Scope is decided in
// docs; parser tests must find BOTH for prefix/exact search.
int network_placeholder = 0;
