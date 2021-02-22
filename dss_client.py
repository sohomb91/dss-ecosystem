#!/usr/bin/python

import os,sys
import dss
from datetime import datetime




class DssClientLib:
    def __init__(self, s3_endpoint, access_key, secret_key, logger_queue=None):
        self.s3_endpoint = "http://" + s3_endpoint
        self.logger_queue = logger_queue
        self.dss_client = self.create_client(self.s3_endpoint, access_key, secret_key)

    def create_client(self, endpoint, access_key, secret_key):
        """
        Create dss_client
        :param endpoint:
        :param access_key:
        :param secret_key:
        :return:
        """
        dss_client = None
        try:
            dss_client =  dss.createClient(endpoint, access_key,secret_key)
            if not dss_client:
                self.logger_queue.put("ERROR: Failed to create s3 client from - {}".format(endpoint))
        except dss.DiscoverError as e:
            self.logger_queue.put("EXCEPTION: {}".format(e))
        except dss.NetworkError as e:
            #print("EXCEPTION: NetworkError - {}".format(e))
            self.logger_queue.put("EXCEPTION: NetworkError - {} , {}".format(endpoint, e))

        return dss_client

    def putObject(self, bucket=None,file=""):

        if file :
            object_key = file
            if file.startswith("/"):
                object_key = file[1:]
            try:
                ret = self.dss_client.putObject(object_key, file)
                if ret == 0:
                    return True
                elif ret == -1:
                    print("ERROR: Upload Failed for  key - {}".format(object_key))
            except dss.NoSuchResouceError as e:
                print("EXCEPTION: NoSuchResourceError putObject - {}".format(e))
            except dss.GenericError as e:
                print("EXCEPTION: putObject {}".format(e))
        return False

    def listObjects(self, bucket=None,  prefix=""):
        object_keys = []
        try:
            object_keys = self.dss_client.listObjects(prefix)
            #if object_keys:
            #    yield object_keys
        except dss.NoSuchResouceError as e:
            print("EXCEPTION: NoSuchResourceError - {}".format(e))
        except dss.GenericError as e:
            print("EXCEPTION: listObjects - {}".format(e))

        return object_keys

    def deleteObject(self, bucket=None, object_key=""):
        self.logger_queue.put("DELETE ......{}".format(object_key))
        if object_key:
            try:
                if self.dss_client.deleteObject(object_key) == 0:
                    return True
                elif self.dss_client.deleteObject(object_key) == -1:
                    print("ERROR: delete object filed for key - {}".format(object_key))
            except dss.NoSuchResouceError as e:
                print("EXECEPTION: deleteOBject - {}, {}".format(object_key,e))
            except dss.GenericError as e:
                print("EXCEPTION: deleteObject - {}".format(e))
        return False


    def getObject(self, bucket=None, object_key="", dest_path=""):
        if object_key and dest_path:
            try:
                return self.dss_client.getObject(object_key, dest_path)
            except dss.NoSuchResouceError as e:
                print("EXCEPTION: getObject - {} , {}".format(object_key, e))
            except dss.GenericError as e:
                print("EXCEPTION: GenericError - {}".format(e))
        return False









if __name__ == "__main__":
    paths = ["/deer/deer1", "/deer/deer2"]

    config="/home/somnath.s/work/nkv-datamover/conf.json"
    start_time = datetime.now()
    dss_client = DssClientLib("202.0.0.103:9000", "minio", "minio123")
    #dss_client = dss.createClient("http://202.0.0.103:9000", "minio", "minio123")
    print("INFO: DSS Client Connection Time: {}".format((datetime.now() - start_time).seconds))

    if dss_client:

        for path in paths:
          for f in os.listdir(path):
            file_path = os.path.abspath(path + "/" + f)
            if not  dss_client.putObject(None, file_path):
                print("Failed to upload file - {}".format(file_path))

        object_keys = dss_client.listObjects("deer/")
        print("ListObjects: {}".format(object_keys))

        object_keys = dss_client.getObjects("deer/")
        print("GetObjects:{}".format(object_keys))




        print("Delete Objects!!!")
        for key in object_keys:
            print("INFO: Key-{}".format(key))
            if not dss_client.deleteObject(None, key):
                print("INFO: Failed to delete Object for key - {}".format(key))



        # Delete

