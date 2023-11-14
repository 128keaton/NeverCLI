/*server.cpp is a server program that receives message,
adds numbers in the message and gives back result to client*/

extern "C" {
  #include <stdio.h>
  #include <stdlib.h>
  #include <stdbool.h>
  #include <string.h>
  #include <unistd.h>
  #include <ctype.h>
  #include <dbus/dbus.h>
  #include <systemd/sd-bus.h>
}

#include <iostream>
#include <string>
#include <fstream>
#include <filesystem>
#include <ctime>
#include <nlohmann/json.hpp>


using json = nlohmann::json;

 const char * const SERVER_BUS_NAME = "copcart.nevercli.CameraStatus";
 const char * const INTERFACE_NAME = "copcart.nevercli.CameraStatus.Manager";
 const char *const OBJECT_PATH_NAME = "/copcart/nevercli/CameraStatus";
 const char * const METHOD_NAME = "new";

std::time_t system_date_time;
/*object representing error name and error message*/
DBusError dbus_error;

/*free error object and destroy message and bus references*/
void free_and_destroy(sd_bus_error *error, sd_bus_message *message, sd_bus *bus){
  sd_bus_error_free(error);
  sd_bus_message_unref(message);
  sd_bus_unref(bus);
  return;
}

/*function to start the requested service*/
std::string request_service(){
  sd_bus_error error = SD_BUS_ERROR_NULL;
  sd_bus_message *reply = NULL;
  sd_bus *bus = NULL;
  const char * path;

  /* create a new independent connection to the system bus */
  int reference = sd_bus_open_system(&bus);
  if (reference < 0) {
    free_and_destroy(&error, reply, bus);
    return (std::string("sd_bus_open_system: ") + std::string(strerror(-reference)));
  }
  
  /*Call StartUnit method on the org.freedesktop.systemd1.Manager interface
   and store the response in reply, otherwise store error in error*/
  reference = sd_bus_call_method(bus, "org.freedesktop.systemd1",
   "/org/freedesktop/systemd1", "org.freedesktop.systemd1.Manager",
   "StartUnit", &error, &reply, "ss", "nvr-record@camera-101.service","replace");
  if (reference < 0) {
    free_and_destroy(&error, reply, bus);
    return (std::string("sd_bus_call_method: ") + std::string(error.message));
  }

  /*parse the reply message into path */
  reference = sd_bus_message_read(reply, "o", &path);
  if (reference < 0) {
     free_and_destroy(&error, reply, bus);
     return (std::string("sd_bus_message_read: ") + std::string(strerror(-reference)));
  }
  
  free_and_destroy(&error, reply, bus);
  system_date_time = std::time(0);
  return (std::string(ctime(&system_date_time)) + std::string("new nvr-record@camera-101.service queued at ") + std::string(path));
}

/*function to print DBusError*/
void print_dbus_error (const char *str) 
{
  fprintf (stderr, "%s: %s\n", str, dbus_error.message);
  /*free error and reinitialize i.e dbus_error_init()*/
  dbus_error_free (&dbus_error);
}


int main(int argc, char **argv ){
  json camera;
  std::filesystem::path directory_path = "/nvr/cameras/";
  if(exists(directory_path)){
    
  }else{
    try{
      std::filesystem::create_directory(directory_path);
    }catch(...){
      std::cout<<SERVER_BUS_NAME<<": unable to create directory "<<directory_path<<std::endl;
      std::cout<<"Permission denied"<<std::endl;
      //exit(1);
    }
  }
  dbus_error_init(&dbus_error);
  /*connection to a login session bus and associated I/O message queues*/	
  DBusConnection *conn;
  conn = dbus_bus_get (DBUS_BUS_SESSION, &dbus_error);
  if(dbus_error_is_set(&dbus_error)){
    print_dbus_error("dbus_bus_get");    
  }
  /*exit with status 1 if no DBusConnection is returned*/
  if(!conn){
    exit(1);
  }
  
  /*request primary ownership of name, return reply to ret*/  
  int ret = dbus_bus_request_name(conn, SERVER_BUS_NAME,
   DBUS_NAME_FLAG_DO_NOT_QUEUE,&dbus_error);/*if not possible do not queue*/
  if(dbus_error_is_set(&dbus_error)){
    print_dbus_error("dbus_bus_request_name");    
  }
  /*if not primary owner of requested name exit with status (1)*/
  if(ret != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER){
    fprintf (stderr, "Dbus: not primary owner, ret = %d\n", ret);
    exit (1);
  }
  
  /*Wait and service request from clients*/
  while (1) {
    /*Block until message to dispatch is received from client*/
    if (!dbus_connection_read_write_dispatch (conn, -1)) { //infnite timeout
      fprintf (stderr, "Not connected now.\n");
      exit (1); /*If timeout exit with status 1*/
    }
    
    /*pop message at the top of incoming message queue.
    If no message continue to wait for incoming messages*/ 
    DBusMessage *message;    
    if ((message = dbus_connection_pop_message (conn)) == NULL) {
      fprintf (stderr, "Did not get message\n");
      continue;
    } 
      
    /*process if message is method call with the given interface and method*/  
    if (dbus_message_is_method_call (message, INTERFACE_NAME, METHOD_NAME)) {
      char *stream_url;
      char *snapshot_url;
      char *output_path;
      char *stream_name;
      int64_t split_every;
      int64_t rtp_port;
      char * ip_address;
      char * rtsp_username;
      char * rtsp_password;
      char *type;
      char *id;
      long i, j;
      bool error = false;
      char *token;
      
      /*get arguments from message given a variable argument list*/
      if (dbus_message_get_args (message, &dbus_error, DBUS_TYPE_STRING,
       &stream_url, DBUS_TYPE_STRING, &snapshot_url, DBUS_TYPE_STRING,
       &output_path, DBUS_TYPE_STRING, &stream_name, DBUS_TYPE_INT64,
       &split_every, DBUS_TYPE_INVALID)) {
        token = strtok(stream_url,"//");
        if(token){
          rtsp_username = strtok(NULL,":");
        }else{
          error = true;
        }
        if(rtsp_username){
          rtsp_password = strtok(NULL,"@");
        }else{
          error = true;
        }
        if(rtsp_password){
          ip_address = strtok(NULL,":");
        }else{
          error = true;
        }
        if(ip_address){
          rtp_port = strtol(strtok(NULL,":"),&stream_url,10);
        }else{
          error = true;
        }

        std::string snapshotURL(snapshot_url);
        std::string rtspUsername(rtsp_username);
        
	if (!error) {
	  camera["streamURL"] = stream_url;
	  camera["snapshotURL"] = (snapshotURL.substr(snapshotURL.find("ISAPI"))).c_str();
	  camera["outputPath"] = output_path;
	  camera["splitEvery"] = split_every;
	  camera["rtpPort"] = rtp_port;
	  camera["ipAddress"] = ip_address;
	  camera["rtspUsername"] = (rtspUsername.substr(1)).c_str();
	  camera["rtspPassword"] = rtsp_password;
	  camera["type"] = "h265";
	  token = strtok(stream_url,"/");
            while(token){
              id = token;          
              //std::cout<<id<<std::endl;
              token = strtok(NULL,"/");
           }
	  camera["id"] = atoi(id);
	  //std::cout<<stream_name<<std::endl;
	  char json_file[] = "camera-";
	  strcat(json_file, id);
	  strcat(json_file, ".json");
	  std::filesystem::path file_path = directory_path/json_file;
	  std::ofstream file(file_path);
	  if(file.is_open()){
	    file << std::setw(4)<<camera;
  	    file.close();
	  }else{
	    std::cout<<SERVER_BUS_NAME<<": unable to create file "<<file_path<<std::endl;
            std::cout<<"Permission denied"<<std::endl;
	  }
  	  
  	  /*start requested service and copy response to reply*/
  	  char request_response[100];
  	  sprintf(request_response, "%s", (char*)(request_service().c_str()));
  	  char * ptr;
  	  ptr = request_response;
  	  //std::cout<<ptr<<std::endl;
  	  DBusMessage *reply;
	  /*construct a DBusMessage reply to received message.
	  if memory cannot be allocated to the reply exit with 1*/
	  if ((reply = dbus_message_new_method_return (message)) == NULL) {
	    fprintf (stderr, "Error in dbus_message_new_method_return\n");
	    exit (1);
	  }

	  DBusMessageIter iter;
	  /*Initialize iter for appending arguments to the end of reply*/
	  dbus_message_iter_init_append (reply, &iter);
	  /*append the basic-typed value to the message.
	  if not enough memory exit with 1 */
	  if (!dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING, &ptr)) {
	    fprintf (stderr, "Error in dbus_message_iter_append_basic\n");
	    exit (1);
	  }
	  /*Add reply to the outgoing message queue.
	  If not enough memory exit with 1*/
	  if (!dbus_connection_send (conn, reply, NULL)) {
	    fprintf (stderr, "Error in dbus_connection_send\n");
	    exit (1);
	  }
	  /*Block until the outgoing message queue is empty*/
          dbus_connection_flush (conn);
          /*Decrement the reference count of reply.
          Free the message if the count reaches 0*/
          dbus_message_unref (reply);	
	}
	else /*invalid arguments*/
	{
	  DBusMessage *dbus_error_msg;
	  char error_msg [] = "Error in input";
	  /*Constructa message that is an error reply to received message*/
	  if ((dbus_error_msg = dbus_message_new_error (message, DBUS_ERROR_FAILED, error_msg)) == NULL) {
	     fprintf (stderr, "Error in dbus_message_new_error\n");
	     exit (1);
	  }

	  if (!dbus_connection_send (conn, dbus_error_msg, NULL)) {
		fprintf (stderr, "Error in dbus_connection_send\n");
		exit (1);
	  }
	  dbus_connection_flush (conn);
	  dbus_message_unref (dbus_error_msg);	
	}
      }
      /*error_getting_message*/
      else
      {
	print_dbus_error ("Error getting message");
      }
    }
  }
  return 0;
}
