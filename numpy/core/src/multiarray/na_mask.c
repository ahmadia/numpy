/*
 * This file implements missing value NA mask support for the NumPy array.
 *
 * Written by Mark Wiebe (mwwiebe@gmail.com)
 * Copyright (c) 2011 by Enthought, Inc.
 *
 * See LICENSE.txt for the license.
 */

#define PY_SSIZE_T_CLEAN
#include <Python.h>

#define NPY_NO_DEPRECATED_API
#define _MULTIARRAYMODULE
#include <numpy/arrayobject.h>

#include "npy_config.h"
#include "numpy/npy_3kcompat.h"

#include "shape.h"
#include "lowlevel_strided_loops.h"

/*NUMPY_API
 *
 * Returns true if the array has an NA mask. When
 * NA dtypes are implemented, will also return true
 * if the array's dtype has NA support.
 */
NPY_NO_EXPORT npy_bool
PyArray_HasNASupport(PyArrayObject *arr)
{
    return PyArray_HASMASKNA(arr);
}

/*NUMPY_API
 *
 * Returns false if the array has no NA support. Returns
 * true if the array has NA support AND there is an
 * NA anywhere in the array.
 */
NPY_NO_EXPORT npy_bool
PyArray_ContainsNA(PyArrayObject *arr)
{
    /* Need NA support to contain NA */
    if (!PyArray_HasNASupport(arr)) {
        return 0;
    }

    /* TODO: Loop through NA mask */

    return 0;
}

/*NUMPY_API
 *
 * If the array does not have an NA mask already, allocates one for it.
 *
 * If 'ownmaskna' is True, it also allocates one for it if the array does
 * not already own its own mask, then copies the data from the old mask
 * to the new mask.
 *
 * If 'multina' is True, the mask is allocated with an NPY_MASK dtype
 * instead of NPY_BOOL.
 *
 * Returns -1 on failure, 0 on success.
 */
NPY_NO_EXPORT int
PyArray_AllocateMaskNA(PyArrayObject *arr, npy_bool ownmaskna, npy_bool multina)
{
    PyArrayObject_fieldaccess *fa = (PyArrayObject_fieldaccess *)arr;
    PyArray_Descr *maskna_dtype = NULL;
    char *maskna_data = NULL;
    npy_intp size;

    /* If the array already owns a mask, done */
    if (fa->flags & NPY_ARRAY_OWNMASKNA) {
        return 0;
    }

    /* If ownership wasn't requested, and there's already a mask, done */
    if (!ownmaskna && (fa->flags & NPY_ARRAY_MASKNA)) {
        return 0;
    }

    size = PyArray_SIZE(arr);

    /* Create the mask dtype */
    if (PyArray_HASFIELDS(arr)) {
        PyErr_SetString(PyExc_RuntimeError,
                "NumPy field-NA isn't supported yet");
        return -1;
    }
    else {
        maskna_dtype = PyArray_DescrFromType(multina ? NPY_MASK
                                                         : NPY_BOOL);
        if (maskna_dtype == NULL) {
            return -1;
        }
    }

    /* Allocate the mask memory */
    maskna_data = PyArray_malloc(size * maskna_dtype->elsize);
    if (maskna_data == NULL) {
        Py_DECREF(maskna_dtype);
        PyErr_NoMemory();
        return -1;
    }

    /* Copy the data and fill in the strides */
    if (fa->nd == 1) {
        /* If there already was a mask copy it, otherwise set it to all ones */
        if (fa->flags & NPY_ARRAY_MASKNA) {
            if (fa->maskna_strides[0] == 1) {
                memcpy(maskna_data, fa->maskna_data,
                            size * maskna_dtype->elsize);
            }
            else {
                if (PyArray_CastRawArrays(fa->dimensions[0],
                                (char *)fa->maskna_data, maskna_data,
                                fa->maskna_strides[0], maskna_dtype->elsize,
                                fa->maskna_dtype, maskna_dtype, 0) < 0) {
                    Py_DECREF(maskna_dtype);
                    PyArray_free(maskna_data);
                    return -1;
                }
            }
        }
        else {
            memset(maskna_data, 1, size * maskna_dtype->elsize);
        }

        fa->maskna_strides[0] = maskna_dtype->elsize;
    }
    else if (fa->nd > 1) {
        _npy_stride_sort_item strideperm[NPY_MAXDIMS];
        npy_intp stride, maskna_strides[NPY_MAXDIMS], *shape;
        int i;

        shape = fa->dimensions;

        /* This causes the NA mask and data memory orderings to match */
        PyArray_CreateSortedStridePerm(fa->nd, fa->strides, strideperm);
        stride = maskna_dtype->elsize;
        for (i = fa->nd-1; i >= 0; --i) {
            npy_intp i_perm = strideperm[i].perm;
            maskna_strides[i_perm] = stride;
            stride *= shape[i_perm];
        }

        /* If there already was a mask copy it, otherwise set it to all ones */
        if (fa->flags & NPY_ARRAY_MASKNA) {
            if (PyArray_CastRawNDimArrays(fa->nd, fa->dimensions,
                            (char *)fa->maskna_data, maskna_data,
                            fa->maskna_strides, maskna_strides,
                            fa->maskna_dtype, maskna_dtype, 0) < 0) {
                Py_DECREF(maskna_dtype);
                PyArray_free(maskna_data);
                return -1;
            }
        }
        else {
            memset(maskna_data, 1, size * maskna_dtype->elsize);
        }

        memcpy(fa->maskna_strides, maskna_strides, fa->nd * sizeof(npy_intp));
    }
    else {
        /* If there already was a mask copy it, otherwise set it to all ones */
        if (fa->flags & NPY_ARRAY_MASKNA) {
            maskna_data[0] = fa->maskna_data[0];
        }
        else {
            maskna_data[0] = 1;
        }
    }

    /* Set the NA mask data in the array */
    Py_XDECREF(maskna_dtype);
    fa->maskna_dtype = maskna_dtype;
    fa->maskna_data = (npy_mask *)maskna_data;
    fa->flags |= (NPY_ARRAY_MASKNA | NPY_ARRAY_OWNMASKNA);

    return 0;
}
