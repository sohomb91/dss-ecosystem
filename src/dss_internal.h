#ifndef DSS_INTERNAL_H
#define DSS_INTERNAL_H

#include "pr.h"

namespace dss {

class Client;
class Cluster;

class CallbackCtx : public Aws::Client::AsyncCallerContext {
public:
	CallbackCtx(Callback func, void* args) {
		cb_func = func;
		cb_args = args;
	}

	Callback getCbFunc() const { return cb_func; }
	void* getCbArgs() const { return cb_args; } 

private:
	Callback cb_func;
	void* cb_args;
};

struct Request {
	typedef Result (Cluster::*Handler) (Request* r);

	Request(const char* k) :
		key(k) {}
	Request(const char* k, const char* f) :
		key(k),
		file(f) {}
	Request(const char* k, const char* f, Callback cb, void* cb_arg) :
		key(k),
		file(f),
		done_func(cb),
		done_arg(cb_arg) {}

	Result Submit(Handler h);

	const std::string	key;
	const std::string	file;
	Callback			done_func;
	void*				done_arg;
	uint32_t			key_hash;
	Cluster*			cluster;
	std::shared_ptr<Aws::IOStream> io_stream;
};

class Result {
public:
	Result() {}
	Result(bool success) : r_success(success) {}
	Result(bool success, Aws::S3::Model::GetObjectResult gor) :
			r_success(success), r_object(std::move(gor)) {}
	Result(bool success, Aws::S3::S3Error e) :
		r_success(success),
		r_err_type(e.GetErrorType()),
		r_err_msg("Exception: " + e.GetExceptionName() +
				  " Details: " + e.GetMessage()) {}

	bool IsSuccess() { return r_success; }
	Aws::IOStream& GetIOStream() { return r_object.GetBody(); }
	Aws::S3::S3Errors GetErrorType() { return r_err_type; }
	Aws::String& GetErrorMsg() { return r_err_msg; }

private:
	bool				r_success;
	Aws::S3::S3Errors 	r_err_type;
	Aws::String			r_err_msg;
	Aws::S3::Model::GetObjectResult	r_object;
};

class Endpoint {
public:
	Endpoint(Credentials& cred, const std::string& url, Config& cfg);

	Result GetObject(const Aws::String& bn, Request* req);
    Result GetObject(const Aws::String& bn, const Aws::String& objectName);
    Result GetObjectAsync(const Aws::String& bn, Request* req);
	Result PutObject(const Aws::String& bn, Request* req);
    Result PutObject(const Aws::String& bn, const Aws::String& objectName, std::shared_ptr<Aws::IOStream>& input_stream);
 	Result PutObjectAsync(const Aws::String& bn, Request* req);
	Result DeleteObject(const Aws::String& bn, Request* req);
    Result DeleteObject(const Aws::String& bn, const Aws::String& objectName);

	Result HeadBucket(const Aws::String& bn);
	Result CreateBucket(const Aws::String& bn);

	Result ListObjects(const Aws::String& bn, Objects *objs);

private:
	Aws::S3::S3Client m_ses;
};

class Cluster {
public:
	Cluster(uint32_t id) :
		m_id(id),
		m_bucket(Aws::String(DATA_BUCKET_PREFIX) + Aws::String(std::to_string(id).c_str())) {}

	~Cluster()
	{
		for (auto e : m_endpoints)
			delete e;
	}

	Endpoint* GetEndpoint(Request* r) { return m_endpoints[r->key_hash % m_endpoints.size()]; }

    Result GetObject(const Aws::String& objectName);
    Result PutObject(const Aws::String& objectName, std::shared_ptr<Aws::IOStream>& input_stream);
    Result DeleteObject(const Aws::String& objectName);

    Result GetObject(Request* r);
	Result GetObjectAsync(Request* r);
    Result PutObject(Request* r);
	Result PutObjectAsync(Request* r);
    Result DeleteObject(Request* r);
 
    Result HeadBucket();
    Result HeadBucket(const Aws::String& bucketName);

	Result CreateBucket();
    uint32_t GetID() { return m_id; }

	Result ListObjects(Objects *objs);

	int InsertEndpoint(Client* c, const std::string& ip, uint32_t port);
private:
	uint32_t m_id;
	Aws::String m_bucket;
   	std::vector<Endpoint*> m_endpoints;

	static constexpr char* DATA_BUCKET_PREFIX = (char*)"dss";
};


/* Not using __attribute__((destructor)) b/c it is only called
   after global var is destructed, so if options is declared
   global, ShutdownAPI() would crash */
class DSSInit {
public:
	DSSInit(): m_local_config(nullptr), m_options()
	{
		char *s = NULL;
		unsigned l = 0;

		if ((s = getenv("DSS_AWS_LOG"))) {
			l = *s - '0';

			if (l > (int) Aws::Utils::Logging::LogLevel::Trace) {
				pr_err("AWS log level out of range\n");
				l = 0;
			}
    		m_options.loggingOptions.logLevel = (Aws::Utils::Logging::LogLevel) l;
		}

		if ((s = getenv("DSS_AWS_LOG_FILENAME"))) {
			m_options.loggingOptions.defaultLogPrefix = s;
		}

		if ((s = getenv("DSS_CONFIG_FILE"))) {
			m_local_config = s;
		}
	
		s = (char*) "AWS_EC2_METADATA_DISABLED=true";
		if (putenv(s))
			pr_err("Failed to set AWS_EC2_METADATA_DISABLED\n");

    	Aws::InitAPI(m_options);
	}

	~DSSInit() 
	{
		Aws::ShutdownAPI(m_options);
	}

	const char* GetConfPath() { return m_local_config; }
	std::mutex& mutex() { return m_mutex; }

private:
	std::mutex m_mutex;
	const char* m_local_config;
	Aws::SDKOptions m_options;
};

class ClusterMap {
public:
	ClusterMap(Client *c, DSSInit& i) :
		m_client(c), m_init(i) {}

	~ClusterMap()
	{
		for (auto c : m_clusters)
			delete c;
	}

	Cluster* InsertCluster(uint32_t id)
	{
		Cluster* c = new Cluster(id);
		if (m_clusters.size() < (id + 1))
			m_clusters.resize(id + 1);
		m_clusters.at(id) = c;

		return c;
	}

	void GetCluster(Request* req);
	const char* GetClusterConfFromLocal() { return m_init.GetConfPath(); }
	int AcquireClusterConf();
	int VerifyClusterConf();

	const std::vector<Cluster*>& GetClusters() { return m_clusters; }

	unsigned GetCLWeight(unsigned i, const Aws::String& key)
	{
		return m_hash(std::to_string(i) + std::string(key.c_str()));
	}

	unsigned GetCLWeight(unsigned i, char* key)
	{
		return m_hash(std::to_string(i) + std::string(key));
	}

	unsigned GetEPWeight(unsigned i);
	
private:
	Client* m_client;
	DSSInit& m_init;
	std::hash<std::string> m_hash;
	std::vector<Cluster*> m_clusters;
}; 
}

#endif // DSS_INTERNAL_H
