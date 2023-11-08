//
// Created by Keaton Burleson on 11/7/23.
//

#include "stream.h"
#include <gst/gst.h>

namespace never {


    int stream::start() {
        auto loop = g_main_loop_new (nullptr, FALSE);

        GstElement *pipeline, *video_src, *depay, *parse, *dec,*enc, *pay, *udp;
        gst_init(nullptr, nullptr);

        pipeline = gst_pipeline_new ("pipeline");

        video_src = gst_element_factory_make("rtspsrc", "src");
        g_object_set(video_src, "location", "rtsp://admin:BlueVip2020@10.2.5.70:554/Streaming/Channels/101/", nullptr);

        depay = gst_element_factory_make("rtph265depay", "depay");
        parse = gst_element_factory_make("h265parse", "parse");
        dec = gst_element_factory_make("avdec_h265", "dec");

        enc = gst_element_factory_make("x264enc", "enc");
        g_object_set(enc, "tune", "fastdecode", nullptr);
        g_object_set(enc, "speed-preset", "ultrafast", nullptr);
        g_object_set(enc, "threads", 1, nullptr);

        pay = gst_element_factory_make("rtph264pay", "pay");
        g_object_set(pay, "config-interval", 1, nullptr);
        g_object_set(pay, "pt", 96, nullptr);
        g_object_set(pay, "aggregate-mode", "zero-latency", nullptr);

        udp = gst_element_factory_make("udpsink", "udp");
        g_object_set(G_OBJECT(udp), "host", "127.0.0.1", NULL);
        g_object_set(G_OBJECT(udp), "port", 5123, NULL);

        gst_bin_add_many (GST_BIN (pipeline), video_src, depay, parse, dec, enc, pay, udp, NULL);

        if (gst_element_link_many (video_src, depay, parse, dec, enc, pay, udp, NULL) != TRUE)
        {
            return -1;
        }

        gst_element_set_state (pipeline, GST_STATE_PLAYING);

        g_main_loop_run (loop);

        gst_element_set_state (pipeline, GST_STATE_NULL);
        gst_object_unref (GST_OBJECT (pipeline));
        g_main_loop_unref (loop);

        return 0;
    }
} // never