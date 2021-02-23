/**
 *   BSD LICENSE
 *
 *   Copyright (c) 2021 Samsung Electronics Co., Ltd.
 *   All rights reserved.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *     * Neither the name of Samsung Electronics Co., Ltd. nor the names of
 *       its contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include "dss.h"

using namespace dss;
namespace py = pybind11;

PYBIND11_MODULE(dss, m) {
	m.def("getVersion", []() {
		return std::string(DSS_VER);
		});

    py::class_<SesOptions>(m, "clientOption")
    	.def(py::init<>())
		.def_readwrite("scheme", &SesOptions::scheme)
		.def_readwrite("useDualStack", &SesOptions::useDualStack)
		.def_readwrite("maxConnections", &SesOptions::maxConnections)
		.def_readwrite("httpRequestTimeoutMs", &SesOptions::httpRequestTimeoutMs)
		.def_readwrite("requestTimeoutMs", &SesOptions::requestTimeoutMs)
		.def_readwrite("connectTimeoutMs", &SesOptions::connectTimeoutMs)
		.def_readwrite("enableTcpKeepAlive", &SesOptions::enableTcpKeepAlive)
		.def_readwrite("tcpKeepAliveIntervalMs", &SesOptions::tcpKeepAliveIntervalMs);

	m.def("createClient", &Client::CreateClient,
		py::arg("url"),
		py::arg("username"),
		py::arg("password"),
		py::arg("options") = SesOptions());
    
    py::class_<Client>(m, "Client")
        .def("putObject", &Client::PutObject,	"Upload object to dss cluster",
        	py::arg("key"),
        	py::arg("file_path"))
        .def("getObject", &Client::GetObject,	"Download object from dss cluster",
        	py::arg("key"),
        	py::arg("file_path"))
        .def("deleteObject", &Client::DeleteObject, "Delete object from dss cluster",
        	py::arg("key"))
        .def("listObjects", &Client::ListObjects, "List object keys with prefix",
        	 py::arg("prefix") = "",
        	 py::arg("delimiter") = "")
		.def("getObjects", &Client::GetObjects, "Create a iterable key list",
			py::arg("prefix") = "",
			py::arg("delimiter") = "",
			py::arg("limit") = DSS_PAGINATION_DEFAULT);

	class NoIterator : std::exception {
	public:
    	const char* what() const noexcept {return "No iterator\n";}
	};

    static py::exception<NoSuchResourceError> NoSuchResourceExc(m, "NoSuchResouceError");
 	static py::exception<DiscoverError> DiscoverExc(m, "DiscoverError");
    static py::exception<NetworkError> NetworkExc(m, "NetworkError");
    static py::exception<GenericError> GenericExc(m, "GenericError");
    static py::exception<NoIterator> LastIterExc(m, "NoIterator");

    py::register_exception_translator([](std::exception_ptr p) {
        try {
            if (p) std::rethrow_exception(p);
        } catch (const NoSuchResourceError &e) {
            NoSuchResourceExc(e.what());
        } catch (const DiscoverError &e) {
            DiscoverExc(e.what());
        } catch (const NetworkError &e) {
            NetworkExc(e.what());
        } catch (const GenericError &e) {
            GenericExc(e.what());
        } catch (const NoIterator &e) {
            LastIterExc(e.what());
        }
    });

	py::class_<Objects>(m, "Objects")
        .def("__iter__", [](Objects &objs) {
        	if (objs.GetObjKeys() < 0)
        		throw NoIterator();
        	return py::make_iterator(objs.begin(), objs.end());
        }, py::keep_alive<0, 1>());
}

