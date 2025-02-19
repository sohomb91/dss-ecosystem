/**
  The Clear BSD License

  Copyright (c) 2022 Samsung Electronics Co., Ltd.
  All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted (subject to the limitations in the disclaimer
  below) provided that the following conditions are met:

 * Redistributions of source code must retain the above copyright notice,
 this list of conditions and the following disclaimer.
 * Redistributions in binary form must reproduce the above copyright notice,
 this list of conditions and the following disclaimer in the documentation
 and/or other materials provided with the distribution.
 * Neither the name of Samsung Electronics Co., Ltd. nor the names of its
 contributors may be used to endorse or promote products derived from this
 software without specific prior written permission.
 NO EXPRESS OR IMPLIED LICENSES TO ANY PARTY'S PATENT RIGHTS ARE GRANTED BY
 THIS LICENSE. THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND
 CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT
 NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 **/

#include <iostream>
#include <fstream>
#include <map>
#include <fcntl.h>
#include <mutex>
#include <sys/stat.h>
#include <unistd.h>
#include <bits/stdc++.h>
#include <linux/limits.h>

#include <aws/core/Aws.h>
#include <aws/core/client/ClientConfiguration.h>
#include <aws/core/auth/AWSCredentials.h>
#include <aws/core/utils/HashingUtils.h>

#include <aws/s3/S3Client.h>
#include <aws/s3/model/GetObjectRequest.h>
#include <aws/s3/model/PutObjectRequest.h>
#include <aws/s3/model/DeleteObjectRequest.h>
#include <aws/s3/model/ListObjectsV2Request.h>
#include <aws/s3/model/CreateBucketRequest.h>
#include <aws/s3/model/DeleteBucketRequest.h>
#include <aws/s3/model/HeadBucketRequest.h>
#include <aws/s3/model/BucketLocationConstraint.h>
#include <aws/s3/model/CommonPrefix.h>
#include <aws/core/utils/stream/PreallocatedStreamBuf.h>


#include "dss.h"
#include "dss_internal.h"
#include "json.hpp"
#include "pr.h"

namespace dss {

	namespace py = pybind11;

	using namespace Aws;

	static const char* DSS_ALLOC_TAG = "DSS";

	DSSInit dss_init;


	Endpoint::Endpoint(Aws::Auth::AWSCredentials& cred, const std::string& url, Config& cfg) 
	{
		cfg.endpointOverride = url.c_str();
		m_ses = Aws::S3::S3Client(cred, cfg, 
				Aws::Client::AWSAuthV4Signer::PayloadSigningPolicy::Never, false);
	}

	Result
		Endpoint::HeadBucket(const Aws::String& bucket)
		{
			Aws::S3::Model::HeadBucketRequest req;
			req.SetBucket(bucket);

			auto&& out = m_ses.HeadBucket(req);

			if (out.IsSuccess())
				return Result(true);
			else
				return Result(false, out.GetError());
		}

	Result
		Endpoint::CreateBucket(const Aws::String& bn)
		{
			Aws::S3::Model::CreateBucketRequest request;
			request.SetBucket(bn);

			auto out = m_ses.CreateBucket(request);

			if (out.IsSuccess())
				return Result(true);
			else
				return Result(false, out.GetError());
		}

	Result
		Endpoint::DeleteBucket(const Aws::String& bn)
		{
			Aws::S3::Model::DeleteBucketRequest request;
			request.SetBucket(bn);

			auto out = m_ses.DeleteBucket(request);

			if (out.IsSuccess())
				return Result(true);
			else
				return Result(false, out.GetError());
		}

	Result
		Endpoint::GetObject(const Aws::String& bn, Request* req)
		{
			Aws::S3::Model::GetObjectRequest ep_req;
			ep_req.WithBucket(bn).SetKey(Aws::String(req->key.c_str()));

			Aws::S3::Model::GetObjectOutcome out = m_ses.GetObject(ep_req);

			if (out.IsSuccess()) {
				return Result(true, out.GetResultWithOwnership());
			} else {
				return Result(false, out.GetError());
			}
		}


	Result
		Endpoint::GetObject(const Aws::String& bn, const Aws::String& objectName)
		{
			Aws::S3::Model::GetObjectRequest req;
			req.WithBucket(bn).SetKey(objectName);

			Aws::S3::Model::GetObjectOutcome out = m_ses.GetObject(req);

			if (out.IsSuccess()) {
				return Result(true, out.GetResultWithOwnership());
			} else {
				return Result(false, out.GetError());
			}
		}

	Result
		Endpoint::GetObject(const Aws::String& bn, Request* req, unsigned char* res_buff, long long buffer_size)
		{
			Aws::S3::Model::GetObjectRequest ep_req;
			ep_req.WithBucket(bn).SetKey(Aws::String(req->key.c_str()));
			Aws::Utils::Stream::PreallocatedStreamBuf streambuf(res_buff, buffer_size);
			ep_req.SetResponseStreamFactory([&streambuf]() { return Aws::New<Aws::IOStream>("", &streambuf); });
			Aws::S3::Model::GetObjectOutcome out = m_ses.GetObject(ep_req);
			if (out.IsSuccess()) {
				return Result(true, out.GetResultWithOwnership().GetContentLength());
			} else {
				return Result(false, out.GetError());
			}
		}

	void GetObjectAsyncDone(const Aws::S3::S3Client* s3Client, 
			const Aws::S3::Model::GetObjectRequest& request, 
			const Aws::S3::Model::GetObjectOutcome& outcome,
			const std::shared_ptr<const Aws::Client::AsyncCallerContext>& context)
	{
		const std::shared_ptr<const CallbackCtx> ctx = 
			std::static_pointer_cast<const CallbackCtx>(context);

		if (outcome.IsSuccess()) {
			Callback cb = ctx->getCbFunc();
			Request* req = (Request*)ctx->getCbArgs();

			std::fstream local_file;
			local_file.open(req->file.c_str(), std::ios::out | std::ios::binary);
			auto& aws_result = outcome.GetResult();
			auto& object_stream = const_cast<Aws::S3::Model::GetObjectResult&>(aws_result).GetBody();

			//TODO: revist performance
			local_file << object_stream.rdbuf();
			local_file.flush();
			local_file.close();

			cb(req->done_arg, req->key,
					outcome.GetError().GetMessage().c_str(), 0);
		} else {
			std::cout << "Error: PutObjectAsyncDone: " <<
				outcome.GetError().GetMessage() << std::endl;
		}

		//upload_variable.notify_one();
	}

	Result
		Endpoint::GetObjectAsync(const Aws::String& bn, Request* req)
		{
			Aws::S3::Model::GetObjectRequest request;
			request.WithBucket(bn).SetKey(Aws::String(req->key.c_str()));

			// Create and configure the context for the asynchronous put object request.
			std::shared_ptr<Aws::Client::AsyncCallerContext> context =
				Aws::MakeShared<CallbackCtx>(DSS_ALLOC_TAG, req->done_func, req);
			context->SetUUID(Aws::String(req->key.c_str()));

			// Make the asynchronous put object call. Queue the request into a 
			// thread executor and call the GetObjectAsyncDone function when the 
			// operation has finished. 
			m_ses.GetObjectAsync(request, GetObjectAsyncDone, context);

			return true;
		}

	void PutObjectAsyncDone(const Aws::S3::S3Client* s3Client, 
			const Aws::S3::Model::PutObjectRequest& request, 
			const Aws::S3::Model::PutObjectOutcome& outcome,
			const std::shared_ptr<const Aws::Client::AsyncCallerContext>& context)
	{
		const std::shared_ptr<const CallbackCtx> ctx = 
			std::static_pointer_cast<const CallbackCtx>(context);

		if (outcome.IsSuccess()) {
			Callback cb = ctx->getCbFunc();
			Request* req = (Request*)ctx->getCbArgs();

			cb(req->done_arg, req->key,
					outcome.GetError().GetMessage().c_str(), 0);
		} else {
			std::cout << "Error: PutObjectAsyncDone: " <<
				outcome.GetError().GetMessage() << std::endl;
		}

		//upload_variable.notify_one();
	}

	Result
		Endpoint::PutObjectAsync(const Aws::String& bn, Request* req)
		{
			// Create and configure the asynchronous put object request.
			Aws::S3::Model::PutObjectRequest request;
			request.WithBucket(bn).SetKey(Aws::String(req->key.c_str()));
			request.SetBody(req->io_stream);

			// Create and configure the context for the asynchronous put object request.
			std::shared_ptr<Aws::Client::AsyncCallerContext> context =
				Aws::MakeShared<CallbackCtx>(DSS_ALLOC_TAG, req->done_func, req);
			context->SetUUID(Aws::String(req->key.c_str()));

			// Make the asynchronous put object call. Queue the request into a 
			// thread executor and call the PutObjectAsyncDone function when the 
			// operation has finished. 
			m_ses.PutObjectAsync(request, PutObjectAsyncDone, context);

			return true;
		}

	Result
		Endpoint::PutObject(const Aws::String& bn, const Aws::String& objectName,
				std::shared_ptr<Aws::IOStream>& input_stream) 
		{
			S3::Model::PutObjectRequest request;
			request.WithBucket(bn).SetKey(objectName);
			request.SetBody(input_stream);

			S3::Model::PutObjectOutcome out = m_ses.PutObject(request);

			if (out.IsSuccess()) {
				return Result(true);
			} else {
				return Result(false, out.GetError());
			}
		}

	Result
		Endpoint::PutObject(const Aws::String& bn, Request* req) 
		{
			S3::Model::PutObjectRequest ep_req;
			ep_req.WithBucket(bn).SetKey(Aws::String(req->key.c_str()));
			ep_req.SetBody(req->io_stream);

			S3::Model::PutObjectOutcome out = m_ses.PutObject(ep_req);

			if (out.IsSuccess()) {
				return Result(true);
			} else {
				return Result(false, out.GetError());
			}
		}

	Result
		Endpoint::PutObject(const Aws::String& bn, Request* req, unsigned char* res_buff, long long content_length)
		{
			S3::Model::PutObjectRequest ep_req;
			ep_req.WithBucket(bn).SetKey(Aws::String(req->key.c_str()));
			Aws::Utils::Stream::PreallocatedStreamBuf streambuf(res_buff, content_length);
			auto preallocated_stream = Aws::MakeShared<Aws::IOStream>("", &streambuf);
			ep_req.SetBody(preallocated_stream);

			S3::Model::PutObjectOutcome out = m_ses.PutObject(ep_req);

			if (out.IsSuccess()) {
				return Result(true);
			} else {
				return Result(false, out.GetError());
			}
		}


	Result
		Endpoint::DeleteObject(const Aws::String& bn, const Aws::String& objectName)
		{
			Aws::S3::Model::DeleteObjectRequest request;

			request.WithBucket(bn).SetKey(objectName);

			auto out = m_ses.DeleteObject(request);

			if (out.IsSuccess()) {
				return Result(true);
			} else {
				return Result(false, out.GetError());
			}
		}

	Result
		Endpoint::DeleteObject(const Aws::String& bn, Request* req)
		{
			Aws::S3::Model::DeleteObjectRequest ep_req;

			ep_req.WithBucket(bn).SetKey(Aws::String(req->key.c_str()));

			auto out = m_ses.DeleteObject(ep_req);

			if (out.IsSuccess()) {
				return Result(true);
			} else {
				return Result(false, out.GetError());
			}
		}

	Result
		Endpoint::ListObjects(const Aws::String& bn, Objects *os)
		{
			bool cont = false;
			std::string token;
			S3::Model::ListObjectsV2Outcome out;
			S3::Model::ListObjectsV2Request req;

			req.WithBucket(bn).WithPrefix(os->GetPrefix()).WithDelimiter(os->GetDelim().c_str());
			if (os->PageSizeSet())
				req.SetMaxKeys(os->GetPageSize());
			if (os->TokenSet())
				req.SetContinuationToken(os->GetToken().c_str());

			do {
				out = m_ses.ListObjectsV2(req);
				if (out.IsSuccess()) {
					//TODO: std::move()
					Aws::Vector<Aws::S3::Model::Object> objects =
						std::move(out.GetResult().GetContents());
					for (auto o : objects)
						os->GetPage().insert(o.GetKey().c_str());

					if (os->NeedCommPrefix()) {
						Aws::Vector<Aws::S3::Model::CommonPrefix> cps =
							std::move(out.GetResult().GetCommonPrefixes());

						for (auto cp : cps) {
							if ((os->GetCPre().find(cp.GetPrefix().c_str())) == os->GetCPre().end()){
								os->GetPage().insert(cp.GetPrefix().c_str());
								os->GetCPre().insert(cp.GetPrefix().c_str());
							}
							else
								continue;
						}            	
					}
				} 
				else {
					return Result(false, out.GetError());
				}
				if (out.GetResult().GetIsTruncated()) {
					if (os->PageSizeSet()) {
						os->SetToken(out.GetResult().GetNextContinuationToken());
						cont = false;
					} else {
						cont = true;
					}
				} else {
					os->SetToken("");
					cont = false;
				}

			} while (cont &&
					(req.SetContinuationToken(out.GetResult().GetNextContinuationToken().c_str()), true));

			return Result(true);
		}

	Result
		ClusterMap::TryLockClusters() {
			return m_client->TryLockClusters();
		}

	Result
		ClusterMap::UnlockClusters() {
			return m_client->UnlockClusters();
		}

	int
		ClusterMap::AcquireClusterConf(const std::string& uuid, const unsigned int endpoint_per_cluster)
		{
			//TODO: redo this function
			Result r;
			std::fstream file;

			if (!GetClusterConfFromLocal()) {
				r = m_client->GetClusterConfig();
				if (!r.IsSuccess()) {
					auto err = r.GetErrorType();
					if (err == Aws::S3::S3Errors::NETWORK_CONNECTION)
						throw NetworkError(r.GetErrorMsg().c_str());

					throw DiscoverError("Failed to download conf.json: " + r.GetErrorMsg());
					return -1;
				}
			} else {
				file.open(GetClusterConfFromLocal(), std::ios::in);
				if (file.fail()) {
					char err_msg[256];
					snprintf(err_msg, 256, "Local config file error (%s): %s\n",
							std::strerror(errno), GetClusterConfFromLocal());
					throw GenericError(err_msg);
					return -1;
				}
			}

			using json = nlohmann::json;

			try {
				json conf;
				if (GetClusterConfFromLocal()){
					conf = json::parse(file);
					file.close();
				} else{
					conf = json::parse(r.GetIOStream());
				}

				try {
					m_wait_time = conf.at("init_time").get<unsigned>();
				} catch (std::exception&) {}

				for (auto &c : conf["clusters"]) {
					std::vector<std::string> hash_vals = {};
					std::map<std::string, int> hash_val_map;
					std::string val;
					unsigned i = 0;
					unsigned ep_count = 0;

					Cluster* cluster = InsertCluster(c["id"], uuid);
					pr_debug("Adding cluster %u\n", (uint32_t)c["id"]);
					for (auto &ep : c["endpoints"]){
						pr_debug("Cluster ID: %u Endpoint %s:%u\n",
								(uint32_t)c["id"], std::string(ep["ipv4"]), (uint32_t)ep["port"]);
						val = GetCLWeight(uuid, std::string(ep["ipv4"]));
						hash_vals.push_back(val);
						push_heap(hash_vals.begin(), hash_vals.end());
						hash_val_map[val] = ep_count;
						ep_count++;
					}
					for (i = 0; i < endpoint_per_cluster && i < ep_count; i++){
						val = hash_vals.front();
						auto ep = c["endpoints"].at(hash_val_map[val]);
						pr_debug("Inserting endpoint Cluster ID: %u EP %s:%u\n",
								(uint32_t)c["id"], std::string(ep["ipv4"]), (uint32_t)ep["port"]);
						cluster->InsertEndpoint(m_client, ep["ipv4"], ep["port"]);
						pop_heap(hash_vals.begin(), hash_vals.end());
						hash_vals.pop_back();
					}

				}
			} catch (std::exception& e) {
				throw DiscoverError("Parse conf.json error: " + Aws::String(e.what()));
			}

			return 0;
		}

	ClusterMap::Status
		ClusterMap::DetectClusterBuckets(bool force)
		{
			const size_t err_len = 256;
			char err_buf[err_len];
			std::vector<bool> empty;
			empty.resize(m_clusters.size());

			std::lock_guard<std::mutex> lock(m_init.mutex());
			for (auto c : m_clusters) {
				Result r = c->HeadBucket();
				if (!r.IsSuccess())
					empty[c->GetID()] = true;
			}

			if (!std::equal(empty.begin() + 1, empty.end(), empty.begin())) {
				uint32_t i = 0;
				std::string err_str;

				for (auto it : empty) {
					if (force) {
						snprintf(err_buf, err_len, "cluster %u : %s\n",
								i++, (unsigned)it ? "missing" : "present");
						err_str.append(err_buf);
					}
				}

				if (force)
					throw NewClientError(err_str);

				return ClusterMap::Status::PARTIAL;
			}

			if (empty[0])
				return ClusterMap::Status::EMPTY;
			else
				return ClusterMap::Status::ALL_GOOD;
		}

	int
		ClusterMap::VerifyClusterConf()
		{
			const size_t err_len = 256;
			char err_buf[err_len];
			Status s = Status::EMPTY;
			State st = State::CREATE;

			while (1) {
				switch (st) {
					case State::CREATE:
						for (auto c : m_clusters) {
							Result r = c->CreateBucket();
							if (r.IsSuccess() ||
									r.GetErrorType() == S3::S3Errors::BUCKET_ALREADY_OWNED_BY_YOU)
								continue;

							snprintf(err_buf, err_len, "Failed to create bucket on cluster %u (msg=%s)\n",
									c->GetID(), r.GetErrorMsg().c_str());

							throw NewClientError(err_buf);
							st = State::EXIT;
							return -1;
						}
						st = State::TEST;
						break;
					case State::TEST:
						//TODO: Wait minio to propagate buckets to other endpoints
						// ask Som
						usleep(m_wait_time * (1ULL << 20));
						s = DetectClusterBuckets(true);
						st = State::EXIT;
						break;
					case State::EXIT:
						if (s == Status::ALL_GOOD)
							return 0;
						else 
							return -1;
				}
			}

			return 0;
		}

	void
		ClusterMap::GetCluster(Request* req)
		{
			unsigned id = 0, max_w = GetCLWeight(0, req->key.c_str());
			for (unsigned i=1; i<m_clusters.size(); i++) {
				unsigned w = GetCLWeight(i, req->key.c_str());
				pr_debug("key %s: cluster %u weight %0x\n", req->key.c_str(), i, w);
				if (w > max_w) {
					id = i;
					max_w = w;
				}
			}

			req->key_hash = max_w;
			req->cluster = m_clusters[id];

			pr_debug("key %s: cluster %u weight %0x\n", req->key.c_str(), id, max_w);
		}

	int
		Cluster::InsertEndpoint(Client* c, const std::string& ip, uint32_t port)
		{
			Endpoint* ep = new Endpoint(c->GetCredential(), ip + ":" + std::to_string(port), c->GetConfig()); 
			m_endpoints.push_back(ep);

			pr_debug("Insert endpoint %s\n", (ip + ":" + std::to_string(port)).c_str());

			return 0;
		}

	Result
		Cluster::HeadBucket()
		{
			return m_endpoints[0]->HeadBucket(m_bucket);
		}

	Result
		Cluster::CreateBucket()
		{
			return m_endpoints[0]->CreateBucket(m_bucket);
		}

	Result
		Cluster::GetObject(const Aws::String& objectName)
		{
			return m_endpoints[0]->GetObject(m_bucket, objectName);
		}

	Result
		Cluster::GetObject(Request* r)
		{
			return GetEndpoint(r)->GetObject(m_bucket, r);
		}

	Result
		Cluster::GetObjectAsync(Request* r)
		{
			return GetEndpoint(r)->GetObjectAsync(m_bucket, r);
		}

	Result
		Cluster::GetObject(Request* r, unsigned char* resp_buff, long long buffer_size)
		{
			return GetEndpoint(r)->GetObject(m_bucket, r, resp_buff, buffer_size);
		}


	Result
		Cluster::PutObject(const Aws::String& objectName, std::shared_ptr<Aws::IOStream>& input_stream)
		{
			return std::move(m_endpoints[0]->PutObject(m_bucket, objectName, input_stream));
		}

	Result
		Cluster::PutObjectAsync(Request* r)
		{
			return GetEndpoint(r)->PutObjectAsync(m_bucket, r);
		}

	Result
		Cluster::PutObject(Request* r)
		{
			return std::move(GetEndpoint(r)->PutObject(m_bucket, r));
		}

	Result
		Cluster::PutObject(Request* r, unsigned char* resp_buff, long long buffer_size)
		{
			return m_endpoints[0]->PutObject(m_bucket, r, resp_buff, buffer_size);
		}


	Result
		Cluster::DeleteObject(const Aws::String& objectName)
		{
			return m_endpoints[0]->DeleteObject(m_bucket, objectName);
		}

	Result
		Cluster::DeleteObject(Request* r)
		{
			return GetEndpoint(r)->DeleteObject(m_bucket, r);
		}

	Result
		Cluster::ListObjects(Objects *objs)
		{
                        std::size_t p_hash = std::hash<std::string> {}(std::to_string(m_id) + m_instance_uuid + std::string(objs->GetPrefix()));
                        return GetEndpoint(p_hash)->ListObjects(m_bucket, objs);
		}

	Result
		Client::GetClusterConfig()
		{
			return m_discover_ep->GetObject(DISCOVER_BUCKET, DISCOVER_CONFIG_KEY);
		}

	Result
		Client::TryLockClusters()
		{
			return m_discover_ep->CreateBucket(LOCK_BUCKET);
		}

	Result
		Client::UnlockClusters()
		{
			return m_discover_ep->DeleteBucket(LOCK_BUCKET);
		}

	Config
		Client::ExtractOptions(const SesOptions& o)
		{
			Aws::Client::ClientConfiguration cfg;
			cfg.scheme = strncasecmp(o.scheme.c_str(), "https", std::string("https").length()) ? 
				Aws::Http::Scheme::HTTP :
				Aws::Http::Scheme::HTTPS;
			cfg.verifySSL = false;
			cfg.useDualStack = o.useDualStack;
			cfg.maxConnections = o.maxConnections;
			cfg.httpRequestTimeoutMs = o.httpRequestTimeoutMs;
			cfg.requestTimeoutMs = o.requestTimeoutMs;
			cfg.connectTimeoutMs = o.connectTimeoutMs;
			cfg.enableTcpKeepAlive = o.enableTcpKeepAlive;
			cfg.tcpKeepAliveIntervalMs = o.tcpKeepAliveIntervalMs;

			return cfg;
		}

	int
		Client::InitClusterMap(const std::string& uuid, const unsigned int endpoints_per_cluster)
		{
			ClusterMap* map = new ClusterMap(this, dss_init);
			if (map->AcquireClusterConf(uuid, endpoints_per_cluster) < 0)
				return -1;
			if (map->VerifyClusterConf() < 0)
				return -1;

			m_cluster_map = map;
			return 0;
		}

	Result
		Request::Submit(Handler handler)
		{
			return ((this->cluster->*handler)(this));
		}

	Result
		Request::Submit_with_buffer(Handler_with_buffer handler, unsigned char* resp_buff, long long buffer_size)
		{
			return ((this->cluster->*handler)(this, resp_buff, buffer_size));
		}


	int Client::GetObject(const Aws::String& objectName, const Aws::String& dest_filename)
	{
		std::unique_ptr<Request> req_guard(new Request(objectName.c_str(), dest_filename.c_str()));

		m_cluster_map->GetCluster(req_guard.get());
		Result r = req_guard->Submit(&Cluster::GetObject);

		if (r.IsSuccess()) {
			std::fstream fs;
			fs.exceptions(std::fstream::failbit | std::fstream::badbit);

			try {
				fs.open(dest_filename.c_str(), std::ios::out | std::ios::binary);
				//TODO: revist performance
				fs << r.GetIOStream().rdbuf();
				fs.flush();
				fs.close();
			} catch (std::exception&) {
				std::string fname(dest_filename.c_str());
				auto e = std::system_error(errno, std::system_category(),
						"Path " + fname);
				throw FileIOError(e.what());
				return -1;
			}

			return 0;
		} else {
			auto err = r.GetErrorType();
			if (err == Aws::S3::S3Errors::RESOURCE_NOT_FOUND)
				throw NoSuchResourceError();
			else
				throw GenericError(r.GetErrorMsg().c_str());

			return -1;
		}
	}

	int Client::GetObjectNumpyBuffer(const Aws::String& objectName, py::array_t<uint8_t> numpy_buffer)
	{
		std::unique_ptr<Request> req_guard(new Request(objectName.c_str()));
		py::buffer_info info = numpy_buffer.request();
		long int buffer_size = numpy_buffer.size();
		auto ptr = static_cast<unsigned char*> (info.ptr);

		m_cluster_map->GetCluster(req_guard.get());

		Result r = req_guard->Submit_with_buffer(&Cluster::GetObject, ptr, buffer_size);

		if (r.IsSuccess()) {
			return r.GetContentLengthValue();
		} else {
			auto err = r.GetErrorType();
			if (err == Aws::S3::S3Errors::RESOURCE_NOT_FOUND)
				throw NoSuchResourceError();
			else
				throw GenericError(r.GetErrorMsg().c_str());

			return -1;
		}
	}

	int Client::GetObjectBuffer(const Aws::String& objectName, py::buffer buffer)
	{
		std::unique_ptr<Request> req_guard(new Request(objectName.c_str()));
		py::buffer_info info = buffer.request();
		long int buffer_size = info.size;
		auto ptr = static_cast<unsigned char*> (info.ptr);

		m_cluster_map->GetCluster(req_guard.get());

		Result r = req_guard->Submit_with_buffer(&Cluster::GetObject, ptr, buffer_size);

		if (r.IsSuccess()) {
			return r.GetContentLengthValue();
		} else {
			auto err = r.GetErrorType();
			if (err == Aws::S3::S3Errors::RESOURCE_NOT_FOUND)
				throw NoSuchResourceError();
			else
				throw GenericError(r.GetErrorMsg().c_str());

			return -1;
		}
	}


	int Client::PutObjectAsync(const std::string& objectName, const std::string& src_fn,
			Callback cb, void* cb_arg)
	{
		Result r;
		struct stat buffer;
		Request* req = new Request(objectName.c_str(), src_fn.c_str(), cb, cb_arg);

		if (stat(src_fn.c_str(), &buffer) == -1) {
			pr_err("Error: PutObjectAsync: File '%s' does not exist.",
					src_fn.c_str());
			return -1;
		}

		req->io_stream = Aws::MakeShared<Aws::FStream>(DSS_ALLOC_TAG,
				src_fn.c_str(),
				std::ios_base::in | std::ios_base::binary);

		m_cluster_map->GetCluster(req);
		r = std::move(req->Submit(&Cluster::PutObjectAsync));

		if (r.IsSuccess()) {
			return 0;
		} else {
			auto err = r.GetErrorType();
			if (err == Aws::S3::S3Errors::RESOURCE_NOT_FOUND)
				throw NoSuchResourceError();
			else
				throw GenericError(r.GetErrorMsg().c_str());

			return -1;
		}
	}

	int Client::GetObjectAsync(const std::string& objectName, const std::string& dst_fn,
			Callback cb, void* cb_arg)
	{
		Result r;

		Request* req = new Request(objectName.c_str(), dst_fn.c_str(), cb, cb_arg);

		m_cluster_map->GetCluster(req);
		r = std::move(req->Submit(&Cluster::GetObjectAsync));
		if (r.IsSuccess()) {
			return 0;
		} else {
			auto err = r.GetErrorType();
			if (err == Aws::S3::S3Errors::RESOURCE_NOT_FOUND)
				throw NoSuchResourceError();
			else
				throw GenericError(r.GetErrorMsg().c_str());

			return -1;
		}
	}

	int Client::PutObject(const Aws::String& objectName, const Aws::String& src_fn, bool async)
	{
		Result r;
		struct stat buffer;
		std::unique_ptr<Request> req_guard(new Request(objectName.c_str(), src_fn.c_str()));

		if (stat(src_fn.c_str(), &buffer) == -1) {
			char msg_buf[PATH_MAX];
			snprintf(msg_buf, PATH_MAX,
					"GetObject: File '%s' does not exist.", src_fn.c_str());
			throw GenericError(msg_buf);
			return false;
		}

		req_guard->io_stream = Aws::MakeShared<Aws::FStream>(DSS_ALLOC_TAG,
				src_fn.c_str(),
				std::ios_base::in | std::ios_base::binary);

		m_cluster_map->GetCluster(req_guard.get());
		if (!async)
			r = std::move(req_guard->Submit(&Cluster::PutObject));
		else
			r = std::move(req_guard->Submit(&Cluster::PutObjectAsync));

		if (r.IsSuccess()) {
			return 0;
		} else {
			auto err = r.GetErrorType();
			if (err == Aws::S3::S3Errors::RESOURCE_NOT_FOUND)
				throw NoSuchResourceError();
			else
				throw GenericError(r.GetErrorMsg().c_str());

			return -1;
		}
	}

	int Client::PutObjectBuffer(const Aws::String& objectName, py::buffer buffer, int content_length)
	{
		py::buffer_info info = buffer.request();
		//long int buffer_size = info.size;
		auto ptr = static_cast<unsigned char*> (info.ptr);

		Result r;
		std::unique_ptr<Request> req_guard(new Request(objectName.c_str()));
		m_cluster_map->GetCluster(req_guard.get());
		r = req_guard->Submit_with_buffer(&Cluster::PutObject, ptr, content_length);

		if (r.IsSuccess()) {
			return 0;
		} else {
			auto err = r.GetErrorType();
			if (err == Aws::S3::S3Errors::RESOURCE_NOT_FOUND)
				throw NoSuchResourceError();
			else
				throw GenericError(r.GetErrorMsg().c_str());

			return -1;
		}
	}


	int Client::DeleteObject(const Aws::String& objectName)
	{
		std::unique_ptr<Request> req_guard(new Request(objectName.c_str()));
		m_cluster_map->GetCluster(req_guard.get());
		Result r = req_guard->Submit(&Cluster::DeleteObject);

		if (r.IsSuccess()) {
			return 0;
		} else {
			auto err = r.GetErrorType();
			if (err == Aws::S3::S3Errors::RESOURCE_NOT_FOUND)
				throw NoSuchResourceError();
			else
				throw GenericError(r.GetErrorMsg().c_str());

			return -1;
		}
	}

	std::set<std::string>&&
		Client::ListObjects(const std::string& prefix, const std::string& delimit)
		{
			std::unique_ptr<Objects> objs = GetObjects(prefix, delimit, 0);
			const std::vector<Cluster*> clusters = m_cluster_map->GetClusters();

			for (auto c : clusters) {
				Result r = c->ListObjects(objs.get());
				if (!r.IsSuccess()) {
					auto err = r.GetErrorType();
					if (err == Aws::S3::S3Errors::RESOURCE_NOT_FOUND)
						throw NoSuchResourceError();
					else
						throw GenericError(r.GetErrorMsg().c_str());
				}
			}

			return std::move(objs->GetPage());
		}

	int Objects::GetObjKeys() 
	{
		const std::vector<Cluster*> clusters = m_cluster_map->GetClusters();

		m_page.clear();

		if (m_cur_id == -1) {
			m_cur_id = 0;
		} else if ((unsigned long)m_cur_id == clusters.size()) {
			m_cur_id = -1;
			return -1;
		}

		while (1) {
			Result r = clusters[m_cur_id]->ListObjects(this);
			if (!r.IsSuccess()) {
				auto err = r.GetErrorType();
				if (err == Aws::S3::S3Errors::RESOURCE_NOT_FOUND)
					throw NoSuchResourceError();
				else
					throw GenericError(r.GetErrorMsg().c_str());
			}

			//pr_debug("cluster %u returns %lu keys\n", m_cur_id, m_page.size());

			if (m_page.size() < GetPageSize()) {
				// TODO: It is unclear whether aws sdk would return
				// # of keys less than pagesize while there are still
				// leftovers in cluster, so we rely on GetIsTruncated()
				// which sets token if true;
				if (!TokenSet()) {
					m_cur_id += 1;
					if ((unsigned long)m_cur_id == clusters.size())
						break;
				}
				continue;
			} else {
				if (!TokenSet())
					m_cur_id += 1;
				break;
			}
		}

		return 0;
	}

	std::unique_ptr<Objects>
		Client::GetObjects(std::string prefix, std::string delimiter, bool cp,
				uint32_t page_size) {
			return std::unique_ptr<Objects>(new Objects(m_cluster_map, prefix, delimiter, cp, page_size));
		};

	Client::Client(const std::string& url, const std::string& user, const std::string& pwd,
			const SesOptions& opts) {
		m_cfg = ExtractOptions(opts);
		m_cred = Aws::Auth::AWSCredentials(user.c_str(), pwd.c_str());
		m_discover_ep = new Endpoint(m_cred, url, m_cfg);
		m_cluster_map = nullptr;
	}

	std::unique_ptr<Client>
		Client::CreateClient(const std::string& ip, const std::string& user,
				const std::string& pwd, const SesOptions& options,
				const std::string& uuid, const unsigned int endpoints_per_cluster)
		{
			std::unique_ptr<Client> client(new Client(ip, user, pwd, options));
			if (client->InitClusterMap(uuid, endpoints_per_cluster) < 0) {
				pr_err("Failed to init cluster map\n");
				return nullptr;
			}

			return client;
		}

	Client::~Client()
	{
		delete m_discover_ep;
		// There could be the case when client is
		// destroyed before cluster_map is init'd
		if (m_cluster_map)
			delete m_cluster_map;
	}

} // namespace dss

