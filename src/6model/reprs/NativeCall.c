#define PARROT_IN_EXTENSION
#include "parrot/parrot.h"
#include "parrot/extend.h"
#include "../sixmodelobject.h"
#include "NativeCall.h"

/* This representation's function pointer table. */
static REPROps *this_repr;

/* Some functions we have to get references to. */
static PMC * (* wrap_object_func) (PARROT_INTERP, void *obj);
static PMC * (* create_stable_func) (PARROT_INTERP, REPROps *REPR, PMC *HOW);

/* Creates a new type object of this representation, and associates it with
 * the given HOW. */
static PMC * type_object_for(PARROT_INTERP, PMC *HOW) {
    /* Create new object instance. */
    NativeCallInstance *obj = mem_allocate_zeroed_typed(NativeCallInstance);

    /* Build an STable. */
    PMC *st_pmc = create_stable_func(interp, this_repr, HOW);
    STable *st  = STABLE_STRUCT(st_pmc);

    /* Create type object and point it back at the STable. */
    obj->common.stable = st_pmc;
    st->WHAT = wrap_object_func(interp, obj);
    PARROT_GC_WRITE_BARRIER(interp, st_pmc);

    /* Flag it as a type object. */
    MARK_AS_TYPE_OBJECT(st->WHAT);

    return st->WHAT;
}

/* Creates a new instance based on the type object. */
static PMC * allocate(PARROT_INTERP, STable *st) {
    NativeCallInstance *obj = mem_allocate_zeroed_typed(NativeCallInstance);
    obj->common.stable = st->stable_pmc;
    return wrap_object_func(interp, obj);
}

/* Initialize a new instance. */
static void initialize(PARROT_INTERP, STable *st, void *data) {
    /* Nothing to do here. */
}

/* Copies to the body of one object to another. */
static void copy_to(PARROT_INTERP, STable *st, void *src, void *dest) {
    NativeCallBody *src_body = (NativeCallBody *)src;
    NativeCallBody *dest_body = (NativeCallBody *)dest;
    
    /* Need a fresh handle for resource management purposes. */
    if (src_body->lib_name) {
        dest_body->lib_name = mem_sys_allocate(strlen(src_body->lib_name) + 1);
        strcpy(dest_body->lib_name, src_body->lib_name);
        dest_body->lib_handle = dlLoadLibrary(dest_body->lib_name);
    }
    
    /* Rest is just simple copying. */
    dest_body->entry_point = src_body->entry_point;
    dest_body->convention = src_body->convention;
    dest_body->num_args = src_body->num_args;
    if (src_body->arg_types) {
        dest_body->arg_types = mem_sys_allocate(src_body->num_args * sizeof(INTVAL));
        memcpy(dest_body->arg_types, src_body->arg_types, src_body->num_args * sizeof(INTVAL));
    }
    dest_body->ret_type = src_body->ret_type;
}

/* This is called to do any cleanup of resources when an object gets
 * embedded inside another one. Never called on a top-level object. */
static void gc_cleanup(PARROT_INTERP, STable *st, void *data) {
    NativeCallBody *body = (NativeCallBody *)data;
    if (body->lib_name)
        Parrot_str_free_cstring(body->lib_name);
    if (body->lib_handle)
        dlFreeLibrary(body->lib_handle);
    if (body->arg_types)
        mem_sys_free(body->arg_types);
}

/* This Parrot-specific addition to the API is used to free an object. */
static void gc_free(PARROT_INTERP, PMC *obj) {
    gc_cleanup(interp, STABLE(obj), OBJECT_BODY(obj));
    mem_sys_free(PMC_data(obj));
    PMC_data(obj) = NULL;
}

/* Gets the storage specification for this representation. */
static storage_spec get_storage_spec(PARROT_INTERP, STable *st) {
    storage_spec spec;
    spec.inlineable = STORAGE_SPEC_INLINED;
    spec.bits = sizeof(NativeCallBody) * 8;
    spec.boxed_primitive = STORAGE_SPEC_BP_NONE;
    return spec;
}

/* Initializes the NativeCall representation. */
REPROps * NativeCall_initialize(PARROT_INTERP,
        PMC * (* wrap_object_func_ptr) (PARROT_INTERP, void *obj),
        PMC * (* create_stable_func_ptr) (PARROT_INTERP, REPROps *REPR, PMC *HOW)) {
    /* Stash away functions passed wrapping functions. */
    wrap_object_func = wrap_object_func_ptr;
    create_stable_func = create_stable_func_ptr;

    /* Allocate and populate the representation function table. */
    this_repr = mem_allocate_zeroed_typed(REPROps);
    this_repr->type_object_for = type_object_for;
    this_repr->allocate = allocate;
    this_repr->initialize = initialize;
    this_repr->copy_to = copy_to;
    this_repr->gc_free = gc_free;
    this_repr->gc_cleanup = gc_cleanup;
    this_repr->get_storage_spec = get_storage_spec;
    return this_repr;
}
