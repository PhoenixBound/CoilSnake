#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <stdio.h>
#include <string.h>

#include "exhal/compress.h"

static PyObject* comp(PyObject* self, PyObject* args) {
	PyObject *list, *clist, *o;
        Py_ssize_t size, i;
        size_t csize;
	long n;
	uint8_t *udata;
	static const pack_options_t compression_options = {1, 0};

	if (!PyArg_ParseTuple(args, "O", &list))
		return NULL;

	if (!PyList_Check(list))
		return PyErr_Format(PyExc_TypeError, "list of numbers expected ('%s' given)", list->ob_type->tp_name);

	size = PyList_Size(list);

	if (size < 1)
		return PyErr_Format(PyExc_TypeError, "got empty list");
	else if (size > 0x10000)
		return PyErr_Format(PyExc_TypeError, "got very long list with %zd items (limit is 65536)", size);

	// Allocate a buffer for both the uncompressed (size) and compressed (up to 65536 bytes) data.
	// In general we can't predict the maximum size of the data after compression, except that the
	// decompression function in-game can't cross banks. exhal is aware of this.
	udata = (uint8_t*) malloc(65536 + size);
	if (!udata)
		return PyErr_NoMemory();


	for (i=0; i < size; ++i) {
		o = PyList_GetItem(list, i);
		if (!PyLong_Check(o)) {
                        free(udata);
			return PyErr_Format(PyExc_TypeError, "list of ints expected ('%s') given", o->ob_type->tp_name);
                }
                n = PyLong_AsLong(o);
		if (n == -1 && PyErr_Occurred()) {
                        free(udata);
			return NULL;
                }
		if (n < 0 || n > 255) {
                        free(udata);
			return PyErr_Format(PyExc_TypeError, "list of ints in range 0-255 expected (%ld found)", n);
                }
                udata[0x10000 + i] = (uint8_t) n;
	}

	csize = exhal_pack2(&udata[0x10000], size, udata, &compression_options);
	if (csize == 0) {
		free(udata);
		return PyErr_Format(PyExc_RuntimeError, "failed to compress %zd bytes of data", size);
	}

	clist = PyList_New(csize);
	if (!clist) {
		free(udata);
		return NULL;
	}
	for (i=0; (size_t)i<csize; ++i) {
		o = PyLong_FromLong((long) udata[i]);
		PyList_SetItem(clist, i, o);
	}
	free(udata);
	return clist;
}

// Given a reference to a Block object, returns a PyByteArray containing its data
// Caches the data from the most recent Block object passed in in order to reduce allocations and marshalling costs
PyObject* get_rom_bytes(PyObject* rom) {
        static PyObject *s_cachedRomArr, *s_cachedRomByteArr;
        PyObject *romArr, *romByteArr;
        Py_ssize_t size;

	romArr = PyObject_GetAttr(rom, PyUnicode_FromString("data"));
        if (!romArr)
                return NULL;
        
        // If rom.data array reference hasn't changed, return the cached byte buffer
        if (romArr == s_cachedRomArr) {
                Py_DECREF(romArr);
                return s_cachedRomByteArr;
        }

	romByteArr = PyByteArray_FromObject(romArr);
        if (!romByteArr)
                return NULL;

        if (!PyByteArray_Check(romByteArr))
                return PyErr_Format(PyExc_TypeError, "bytearray of numbers expected ('%s') given", romArr->ob_type->tp_name);

        size = PyByteArray_Size(romByteArr);

        if (size < 1)
                return PyErr_Format(PyExc_TypeError, "rom's data attribute was empty");

        // We are replacing the cached references, so release them before we do
        Py_XDECREF(s_cachedRomArr);
        Py_XDECREF(s_cachedRomByteArr);
        
        // Because this reference to the array is cached, there will always be an extra reference
        // to the rom's data until the program ends
        s_cachedRomArr = romArr;
        s_cachedRomByteArr = romByteArr;
        
        return romByteArr;
}

static PyObject* decomp(PyObject* self, PyObject* args) {
	PyObject *rom, *ulist, *o, *romByteArr;
	int addr;
	size_t new_size, i;
	uint8_t *romBuffer, *buffer;

	if (!PyArg_ParseTuple(args, "Oi", &rom, &addr))
		return NULL;

	romByteArr = get_rom_bytes(rom);
	if (!romByteArr)
		return NULL;

	romBuffer = (uint8_t*) PyByteArray_AS_STRING(romByteArr);

	// Allocate a buffer
	buffer = (uint8_t*) malloc(65536);
	if (!buffer)
		return PyErr_NoMemory();

	new_size = exhal_unpack(romBuffer + addr, buffer, NULL);
	if (new_size == 0) {
		free(buffer);
		return PyErr_Format(PyExc_RuntimeError, "failed to decompress data at address $%06x", addr);
	}

	ulist = PyList_New(new_size);
	if (!ulist) {
		free(buffer);
		return NULL;
	}
	for (i=0; i<new_size; ++i) {
		o = PyLong_FromLong((long) buffer[i]);
		PyList_SetItem(ulist, i, o);
	}
	free(buffer);
	return ulist;
}

static PyMethodDef native_comp_methods[] = {
	{"comp", comp, METH_VARARGS, "C implementation of EB's comp()"},
	{"decomp", decomp, METH_VARARGS, "C implementation of EB's decomp()"},
	{NULL, NULL, 0, NULL}
};

struct module_state {
    PyObject *error;
};

#define GETSTATE(m) ((struct module_state*)PyModule_GetState(m))

static int native_comp_traverse(PyObject *m, visitproc visit, void *arg) {
    Py_VISIT(GETSTATE(m)->error);
    return 0;
}

static int native_comp_clear(PyObject *m) {
    Py_CLEAR(GETSTATE(m)->error);
    return 0;
}

static struct PyModuleDef moduledef = {
        PyModuleDef_HEAD_INIT,
        "native_comp",
        NULL,
        sizeof(struct module_state),
        native_comp_methods,
        NULL,
        native_comp_traverse,
        native_comp_clear,
        NULL
};

PyMODINIT_FUNC
PyInit_native_comp(void)
{
	PyObject *module = PyModule_Create(&moduledef);
        return module;
}
