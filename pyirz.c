#include "Python.h"

static PyObject *
pyirz_resize(PyObject *self, PyObject *args, PyObject *keywds) {
	Py_INCREF(Py_None);
	return Py_None;
}

static PyMethodDef pyirz_methods[] = {
	{"resize", pyirz_resize, METH_VARARGS | METH_KEYWORDS, "Rezize a jpeg"},
	{NULL, NULL, 0, NULL}	/* sentinel */
};

void
initpyirz(void) {
	Py_InitModule("pyirz", pyirz_methods);
}