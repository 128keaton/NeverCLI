/*
 *
 *     add-client.c: client program, takes two numbers as input,
 *                   sends to server for addition,
 *                   gets result from server,
 *                   prints the result on the screen
 *
 */
extern "C"{ 
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h> 
#include <string.h>
#include <stdbool.h>
#include <ctype.h>

#include <dbus/dbus.h>
}
#include <iostream>
#include <string>

using namespace std;
const char *const INTERFACE_NAME = "copcart.nevercli.CameraStatus.Manager";
const char *const SERVER_BUS_NAME = "copcart.nevercli.CameraStatus";
char CLIENT_BUS_NAME[] = "copcart.nevercli.client";
const char *const SERVER_OBJECT_PATH_NAME = "/copcart/nevercli/CameraStatus";
const char *const CLIENT_OBJECT_PATH_NAME = "/copcart/nevercli/client";
const char *const METHOD_NAME = "new";

DBusError dbus_error;
void print_dbus_error (const char *str);

int main (int argc, char **argv)
{
    if(argc!=6){
      printf("usage: %s RTSP_STREAM_URL SNAPSHOT_URL STREAM_NAME OUTPUT_PATH SPLIT_EVERY\n"
               "i.e. %s rtsp://0.0.0.0 http://0.0.0.0 ./ camera1 60\n"
               "Write an RTSP stream to file.\n"
               "\n", argv[0], argv[0]);
      exit(1);
    }
    
    const char *stream_url = argv[1],
            *snapshot_url = argv[2],
            *output_path = argv[3],
            *stream_name = argv[4];
    const long clip_runtime = strtol(argv[5], nullptr, 10);
    
    
    dbus_error_init (&dbus_error);
    DBusConnection *conn;

    conn = dbus_bus_get (DBUS_BUS_SESSION, &dbus_error);

    if (dbus_error_is_set (&dbus_error)){
        print_dbus_error ("dbus_bus_get");
    }
    if (!conn){
        exit (1);
    }
    // Get a well known name
    strcat(CLIENT_BUS_NAME,stream_name);
    //cout<<CLIENT_BUS_NAME<<endl;
    int request_name_reply;
    while(1){
      request_name_reply = dbus_bus_request_name (conn,CLIENT_BUS_NAME , 0, &dbus_error);
      if (request_name_reply == DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER){
        break;
      }else if(request_name_reply == DBUS_REQUEST_NAME_REPLY_IN_QUEUE){
        cout<<CLIENT_BUS_NAME<<"waiting in queue"<<endl;
	continue;
      }
    }
    if (dbus_error_is_set (&dbus_error)){
       print_dbus_error ("dbus_bus_get");
       exit(1);
    }
    
    DBusMessage *request;
    if((request = dbus_message_new_method_call (SERVER_BUS_NAME,
     SERVER_OBJECT_PATH_NAME, INTERFACE_NAME, METHOD_NAME)) == NULL) {
      fprintf (stderr, "Error in dbus_message_new_method_call\n");
      exit (1);
    }

   DBusMessageIter iter;
   dbus_message_iter_init_append (request, &iter);
   if (!dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING, &stream_url)) {
     fprintf (stderr, "Error in dbus_message_iter_append_basic\n");
     exit (1);
   }
   if (!dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING, &snapshot_url)) {
     fprintf (stderr, "Error in dbus_message_iter_append_basic\n");
     exit (1);
   }
   if (!dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING, &output_path)) {
     fprintf (stderr, "Error in dbus_message_iter_append_basic\n");
     exit (1);
   }
   if (!dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING, &stream_name)) {
     fprintf (stderr, "Error in dbus_message_iter_append_basic\n");
     exit (1);
   }
   if (!dbus_message_iter_append_basic (&iter, DBUS_TYPE_INT64, &clip_runtime)) {
     fprintf (stderr, "Error in dbus_message_iter_append_basic\n");
     exit (1);
   }
   DBusPendingCall *pending_call;
   if (!dbus_connection_send_with_reply (conn, request, &pending_call, -1)) {
     fprintf (stderr, "Error in dbus_connection_send_with_reply\n");
     exit (1);
   }
   if (pending_call == NULL) {
     fprintf (stderr, "pending return is NULL");
     exit (1);
   }

   dbus_connection_flush (conn);
   dbus_message_unref (request);
   dbus_pending_call_block (pending_call);

   DBusMessage *reply;
   if ((reply = dbus_pending_call_steal_reply (pending_call)) == NULL) {
     fprintf (stderr, "Error in dbus_pending_call_steal_reply");
     exit (1);   
  }
  dbus_pending_call_unref (pending_call);
  
  char *s;
  if (dbus_message_get_args (reply, &dbus_error, DBUS_TYPE_STRING, &s, DBUS_TYPE_INVALID)) {
    printf ("%s\n", s);
  }else{
    fprintf (stderr, "No reply\n");
    exit (1);
  }
  dbus_message_unref (reply);

  if(dbus_bus_release_name (conn, CLIENT_BUS_NAME, &dbus_error) == -1) {
     fprintf (stderr, "Error in dbus_bus_release_name\n");
     exit (1);
  }
  return 0;
}

void print_dbus_error (const char *str) 
{
    fprintf (stderr, "%s: %s\n", str, dbus_error.message);
    dbus_error_free (&dbus_error);
}
