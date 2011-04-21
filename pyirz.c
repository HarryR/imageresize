/*
Copyright (c) 2011 Harry Roberts.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

#include "irz.h"

#include "Python.h"

#include <string.h>

typedef struct {
	PyObject_HEAD
	irz_config_t cfg;
} pyirz_ResizeObject;

static PyTypeObject pyirz_ResizeType = {
	PyObject_HEAD_INIT(NULL)
	0,                         /*ob_size*/
	"pyirz.Resize",            /*tp_name*/
	sizeof(pyirz_ResizeObject), /*tp_basicsize*/
	0,                         /*tp_itemsize*/
	0,                         /*tp_dealloc*/
	0,                         /*tp_print*/
	0,                         /*tp_getattr*/
	0,                         /*tp_setattr*/
	0,                         /*tp_compare*/
	0,                         /*tp_repr*/
	0,                         /*tp_as_number*/
	0,                         /*tp_as_sequence*/
	0,                         /*tp_as_mapping*/
	PyObject_HashNotImplemented,                         /*tp_hash */
	0,                         /*tp_call*/
	0,                         /*tp_str*/
	0,                         /*tp_getattro*/
	0,                         /*tp_setattro*/
	0,                         /*tp_as_buffer*/
	Py_TPFLAGS_DEFAULT,        /*tp_flags*/
	"Resize Object",           /* tp_doc */
	NULL,			   /* tp_traverse */
	NULL,			   /* tp_clear */
	NULL,			   /* tp_richcompare */	
	0,			   /* tp_weaklistoffset */
	NULL,			   /* tp_iter */
	NULL,			   /* tp_iternext */
	NULL,			   /* tp_methods */
	NULL,			   /* tp_members */
	NULL,			   /* tp_getset */
	NULL,			   /* tp_base */
	NULL,			   /* tp_dict */
	NULL,			   /* tp_descr_get */
	NULL,			   /* tp_descr_set */
	0,			   /* tp_dictoffset */
	NULL,			   /* tp_init */
	PyType_GenericAlloc,	   /* tp_alloc */
	PyType_GenericNew,	   /* tp_new */
	_PyObject_Del,		   /* tp_free */
	NULL,			   /* tp_is_gc */
	NULL,			   /* tp_bases */
	NULL,			   /*tp_mro*/
	NULL,			   /*tp_cache*/
	NULL,			   /*tp_subclasses*/
	NULL,			   /*tp_weaklist*/
	NULL,			   /*tp_del*/
	0,			   /* tp_version_tag */
	#ifdef COUNT_ALLOCS
	0,                         /*tp_allocs*/
	0,                         /*tp_frees*/
	0,                         /*tp_maxalloc*/
	0,                         /*tp_prev*/
	0,                         /*tp_next*/
	#endif
};

static int
pyirz_Resize_init(pyirz_ResizeObject *self, PyObject *args, PyObject *kwds) {
	irz_config_t *cfg = &self->cfg;
	memset(cfg, 0, sizeof(irz_config_t));
	cfg->mode = MODE_SCALE;
	return 1;
}

static PyMethodDef pyirz_methods[] = {
	{NULL, NULL, 0, NULL}	/* sentinel */
};

#ifndef PyMODINIT_FUNC	/* declarations for DLL import/export */
#define PyMODINIT_FUNC void
#endif
PyMODINIT_FUNC
initpyirz(void) {
	PyObject *m;
	
	pyirz_ResizeType.tp_new = PyType_GenericNew;
	if( PyType_Ready(&pyirz_ResizeType) < 0 ) {
		return;
	}
		
	m = Py_InitModule("pyirz", pyirz_methods);
	Py_INCREF(&pyirz_ResizeType);
	PyModule_AddObject(m, "Resize", (PyObject *)&pyirz_ResizeType);
}