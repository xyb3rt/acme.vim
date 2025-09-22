#ifndef VEC_H
#define VEC_H

#define container_of(ptr, type, member) \
	((type *)((char *)(ptr) - offsetof(type, member)))

#define vec_free(v) \
	_vec_free((void **)(v))
#define vec_clear(v) \
	_vec_clear((void **)(v))
#define vec_len(v) \
	_vec_len((void **)(v))
#define vec_dig(v, i, n) \
	((typeof(*(v)))_vec_dig((void **)(v), (i), (n), sizeof(**(v))))
#define vec_insert(v, i, val) \
	(*vec_dig((v), (i), 1) = (val))
#define vec_push(v, val) \
	(*vec_dig((v), -1, 1) = (val))
#define vec_erase(v, i, n) \
	_vec_erase((void **)(v), (i), (n), sizeof(**(v)))
#define vec_find(v, val, cmp) \
	_vec_find((void **)(v), (val), sizeof(**(v)), (cmp))
#define vec_findi(v, val) vec_find((v), (val), vec_cmpint)
#define vec_finds(v, val) vec_find((v), (intptr_t)(val), vec_cmpstr)

struct vec {
	size_t cap;
	size_t len;
	char d[];
};

static void *vec_new(void) {
	struct vec *vec = malloc(sizeof(*vec));
	if (vec == NULL) {
		error(EXIT_FAILURE, errno, "vec_new");
	}
	memset(vec, 0, sizeof(*vec));
	return &vec->d;
}

static void _vec_free(void **d) {
	if (*d != NULL) {
		struct vec *vec = container_of(*d, struct vec, d);
		free(vec);
		*d = NULL;
	}
}

static void _vec_clear(void **d) {
	container_of(*d, struct vec, d)->len = 0;
}

static size_t _vec_len(void **d) {
	return container_of(*d, struct vec, d)->len;
}

static void *_vec_dig(void **d, size_t i, size_t n, size_t sz) {
	struct vec *vec = container_of(*d, struct vec, d);
	if (n == 0 || sz == 0) {
		return NULL;
	}
	if (i > vec->len) {
		i = vec->len;
	}
	size_t cap = vec->cap != 0 ? vec->cap : 16;
	while (cap < vec->len + n) {
		if (sizeof(*vec) + cap * sz * 2 < sizeof(*vec) + cap * sz) {
			error(EXIT_FAILURE, ENOMEM, "vec_dig");
		}
		cap *= 2;
	}
	if (vec->cap != cap) {
		vec = (struct vec *)realloc(vec, sizeof(*vec) + cap * sz);
		if (vec == NULL) {
			error(EXIT_FAILURE, errno, "vec_dig");
		}
		vec->cap = cap;
		*d = &vec->d;
	}
	memmove(&vec->d[(i + n) * sz], &vec->d[i * sz], (vec->len - i) * sz);
	vec->len += n;
	return &vec->d[i * sz];
}

static void _vec_erase(void **d, size_t i, size_t n, size_t sz) {
	struct vec *vec = container_of(*d, struct vec, d);
	if (i >= vec->len || n == 0 || sz == 0) {
		return;
	}
	if (n > vec->len - i) {
		n = vec->len - i;
	}
	vec->len -= n;
	memmove(&vec->d[i * sz], &vec->d[(i + n) * sz], (vec->len - i) * sz);
}

static size_t _vec_find(void **d, intptr_t val, size_t sz,
                        int (*cmp)(void *, intptr_t)) {
	struct vec *vec = container_of(*d, struct vec, d);
	for (size_t i = 0; i < vec->len; i++) {
		if (cmp(&vec->d[i * sz], val) == 0) {
			return i;
		}
	}
	return -1;
}

static int vec_cmpint(void *elem, intptr_t val) {
	return *(unsigned int *)elem - val;
}

static int vec_cmpstr(void *elem, intptr_t val) {
	return strcmp(*(const char **)elem, (const char *)val);
}

#endif /* VEC_H */
