/* gst_bridge
 * Copyright (C) 2020-2021 Brett Downing <brettrd@brettrd.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/**
 * SECTION:element-roscompressedimagesink
 *
 * The roscompressedimagesink element accepts compressed GStreamer buffers
 * (image/jpeg, video/x-h264, video/x-h265) and publishes them as
 * sensor_msgs/msg/CompressedImage messages.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch-1.0 -v videotestsrc ! jpegenc ! roscompressedimagesink ros-topic=/camera/compressed
 * ]|
 * Publishes JPEG-compressed video as ROS CompressedImage messages.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst_bridge/roscompressedimagesink.h>

GST_DEBUG_CATEGORY_STATIC(roscompressedimagesink_debug_category);
#define GST_CAT_DEFAULT roscompressedimagesink_debug_category

/* prototypes */

static void roscompressedimagesink_set_property(
  GObject * object, guint property_id, const GValue * value, GParamSpec * pspec);
static void roscompressedimagesink_get_property(
  GObject * object, guint property_id, GValue * value, GParamSpec * pspec);

static void roscompressedimagesink_init(Roscompressedimagesink * sink);

static gboolean roscompressedimagesink_open(RosBaseSink * ros_base_sink);
static gboolean roscompressedimagesink_close(RosBaseSink * ros_base_sink);
static gboolean roscompressedimagesink_setcaps(GstBaseSink * gst_base_sink, GstCaps * caps);
static GstFlowReturn roscompressedimagesink_render(
  RosBaseSink * ros_base_sink, GstBuffer * buffer, rclcpp::Time msg_time);

enum {
  PROP_0,
  PROP_ROS_TOPIC,
  PROP_ROS_FRAME_ID,
  PROP_ROS_ENCODING,
};

/* pad templates */

static GstStaticPadTemplate roscompressedimagesink_sink_template = GST_STATIC_PAD_TEMPLATE(
  "sink", GST_PAD_SINK, GST_PAD_ALWAYS,
  GST_STATIC_CAPS(ROS_COMPRESSED_IMAGE_MSG_CAPS));

/* class initialization */

G_DEFINE_TYPE_WITH_CODE(
  Roscompressedimagesink, roscompressedimagesink, GST_TYPE_ROS_BASE_SINK,
  GST_DEBUG_CATEGORY_INIT(
    roscompressedimagesink_debug_category, "roscompressedimagesink", 0,
    "debug category for roscompressedimagesink element"))

static void roscompressedimagesink_class_init(RoscompressedimagesinkClass * klass)
{
  GObjectClass * object_class = G_OBJECT_CLASS(klass);
  GstElementClass * element_class = GST_ELEMENT_CLASS(klass);
  GstBaseSinkClass * basesink_class = GST_BASE_SINK_CLASS(klass);
  RosBaseSinkClass * ros_base_sink_class = GST_ROS_BASE_SINK_CLASS(klass);

  object_class->set_property = roscompressedimagesink_set_property;
  object_class->get_property = roscompressedimagesink_get_property;

  gst_element_class_add_static_pad_template(element_class, &roscompressedimagesink_sink_template);

  gst_element_class_set_static_metadata(
    element_class, "roscompressedimagesink", "Sink",
    "a gstreamer sink that publishes compressed image data into ROS",
    "BrettRD <brettrd@brettrd.com>");

  g_object_class_install_property(
    object_class, PROP_ROS_TOPIC,
    g_param_spec_string(
      "ros-topic", "pub-topic", "ROS topic to be published on", "gst_compressed_image_pub",
      (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property(
    object_class, PROP_ROS_FRAME_ID,
    g_param_spec_string(
      "ros-frame-id", "frame-id", "frame_id of the compressed image message", "",
      (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property(
    object_class, PROP_ROS_ENCODING,
    g_param_spec_string(
      "ros-encoding", "encoding-string",
      "Override CompressedImage.format string (e.g. jpeg, h264, h265). "
      "If empty, format is auto-detected from caps.",
      "", (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  basesink_class->set_caps = GST_DEBUG_FUNCPTR(roscompressedimagesink_setcaps);

  ros_base_sink_class->open = GST_DEBUG_FUNCPTR(roscompressedimagesink_open);
  ros_base_sink_class->close = GST_DEBUG_FUNCPTR(roscompressedimagesink_close);
  ros_base_sink_class->render = GST_DEBUG_FUNCPTR(roscompressedimagesink_render);
}

static void roscompressedimagesink_init(Roscompressedimagesink * sink)
{
  RosBaseSink * ros_base_sink = GST_ROS_BASE_SINK(sink);
  ros_base_sink->node_name = g_strdup("gst_compressed_image_sink_node");
  sink->pub_topic = g_strdup("gst_compressed_image_pub");
  sink->frame_id = g_strdup("image_frame");
  sink->ros_format = g_strdup("");
}

static void roscompressedimagesink_set_property(
  GObject * object, guint property_id, const GValue * value, GParamSpec * pspec)
{
  RosBaseSink * ros_base_sink = GST_ROS_BASE_SINK(object);
  Roscompressedimagesink * sink = GST_ROSCOMPRESSEDIMAGESINK(object);

  GST_DEBUG_OBJECT(sink, "set_property");

  switch (property_id) {
    case PROP_ROS_TOPIC:
      if (ros_base_sink->node_if) {
        RCLCPP_ERROR(
          ros_base_sink->node_if->logging->get_logger(), "can't change topic name once opened");
      } else {
        g_free(sink->pub_topic);
        sink->pub_topic = g_value_dup_string(value);
      }
      break;

    case PROP_ROS_FRAME_ID:
      g_free(sink->frame_id);
      sink->frame_id = g_value_dup_string(value);
      break;

    case PROP_ROS_ENCODING:
      g_free(sink->ros_format);
      sink->ros_format = g_value_dup_string(value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
      break;
  }
}

static void roscompressedimagesink_get_property(
  GObject * object, guint property_id, GValue * value, GParamSpec * pspec)
{
  Roscompressedimagesink * sink = GST_ROSCOMPRESSEDIMAGESINK(object);

  GST_DEBUG_OBJECT(sink, "get_property");
  switch (property_id) {
    case PROP_ROS_TOPIC:
      g_value_set_string(value, sink->pub_topic);
      break;

    case PROP_ROS_FRAME_ID:
      g_value_set_string(value, sink->frame_id);
      break;

    case PROP_ROS_ENCODING:
      g_value_set_string(value, sink->ros_format);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
      break;
  }
}

static gboolean roscompressedimagesink_open(RosBaseSink * ros_base_sink)
{
  Roscompressedimagesink * sink = GST_ROSCOMPRESSEDIMAGESINK(ros_base_sink);
  GST_DEBUG_OBJECT(sink, "open");

  rclcpp::QoS qos = rclcpp::QoS(1).best_effort().durability_volatile();

  sink->pub = rclcpp::create_publisher<sensor_msgs::msg::CompressedImage>(
    ros_base_sink->node_if->parameters, ros_base_sink->node_if->topics, sink->pub_topic, qos);

  return TRUE;
}

static gboolean roscompressedimagesink_close(RosBaseSink * ros_base_sink)
{
  Roscompressedimagesink * sink = GST_ROSCOMPRESSEDIMAGESINK(ros_base_sink);
  GST_DEBUG_OBJECT(sink, "close");
  sink->pub.reset();
  return TRUE;
}

static gboolean roscompressedimagesink_setcaps(GstBaseSink * gst_base_sink, GstCaps * caps)
{
  RosBaseSink * ros_base_sink = GST_ROS_BASE_SINK(gst_base_sink);
  Roscompressedimagesink * sink = GST_ROSCOMPRESSEDIMAGESINK(ros_base_sink);

  GST_DEBUG_OBJECT(sink, "setcaps");

  if (ros_base_sink->node_if)
    RCLCPP_INFO(
      ros_base_sink->node_if->logging->get_logger(), "preparing compressed video with caps '%s'",
      gst_caps_to_string(caps));

  // Only auto-detect format if the user hasn't overridden it via property
  if (0 == g_strcmp0(sink->ros_format, "")) {
    GstStructure * caps_struct = gst_caps_get_structure(caps, 0);
    const gchar * caps_name = gst_structure_get_name(caps_struct);

    if (0 == g_strcmp0(caps_name, "image/jpeg")) {
      g_free(sink->ros_format);
      sink->ros_format = g_strdup("jpeg");
    } else if (0 == g_strcmp0(caps_name, "video/x-h264")) {
      g_free(sink->ros_format);
      sink->ros_format = g_strdup("h264");
    } else if (0 == g_strcmp0(caps_name, "video/x-h265")) {
      g_free(sink->ros_format);
      sink->ros_format = g_strdup("h265");
    } else {
      if (ros_base_sink->node_if)
        RCLCPP_WARN(
          ros_base_sink->node_if->logging->get_logger(),
          "roscompressedimagesink: unknown caps '%s'", caps_name);
      GST_WARNING_OBJECT(sink, "unknown caps name '%s'", caps_name);
      return FALSE;
    }
  }

  if (ros_base_sink->node_if)
    RCLCPP_INFO(
      ros_base_sink->node_if->logging->get_logger(),
      "roscompressedimagesink: using format '%s'", sink->ros_format);

  return TRUE;
}

static GstFlowReturn roscompressedimagesink_render(
  RosBaseSink * ros_base_sink, GstBuffer * buf, rclcpp::Time msg_time)
{
  GstMapInfo info;
  sensor_msgs::msg::CompressedImage msg;

  Roscompressedimagesink * sink = GST_ROSCOMPRESSEDIMAGESINK(ros_base_sink);
  GST_DEBUG_OBJECT(sink, "render");

  msg.header.stamp = msg_time;
  msg.header.frame_id = sink->frame_id;
  msg.format = sink->ros_format;

  gst_buffer_map(buf, &info, GST_MAP_READ);
  msg.data.assign(info.data, info.data + info.size);
  gst_buffer_unmap(buf, &info);

  sink->pub->publish(msg);

  return GST_FLOW_OK;
}
