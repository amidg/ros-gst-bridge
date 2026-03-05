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
 * SECTION:element-roscompressedimagesrc
 *
 * The roscompressedimagesrc element subscribes to a sensor_msgs/CompressedImage
 * topic and pushes the compressed bytes into a GStreamer pipeline.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch-1.0 -v roscompressedimagesrc ros-topic=/camera/compressed ! jpegdec ! videoconvert ! autovideosink
 * ]|
 * Decodes and displays a JPEG compressed ROS image topic.
 * </refsect2>
 */

#include <gst_bridge/roscompressedimagesrc.h>

GST_DEBUG_CATEGORY_STATIC(roscompressedimagesrc_debug_category);
#define GST_CAT_DEFAULT roscompressedimagesrc_debug_category

/* prototypes */

static void roscompressedimagesrc_set_property(
  GObject * object, guint property_id, const GValue * value, GParamSpec * pspec);
static void roscompressedimagesrc_get_property(
  GObject * object, guint property_id, GValue * value, GParamSpec * pspec);

static void roscompressedimagesrc_init(Roscompressedimagesrc * src);

static gboolean roscompressedimagesrc_open(RosBaseSrc * ros_base_src);
static gboolean roscompressedimagesrc_close(RosBaseSrc * ros_base_src);
static GstFlowReturn roscompressedimagesrc_create(
  GstBaseSrc * base_src, guint64 offset, guint size, GstBuffer ** buf);
static gboolean roscompressedimagesrc_notify_thread(RosBaseSrc * ros_base_src);
static gboolean roscompressedimagesrc_query(GstBaseSrc * base_src, GstQuery * query);
static GstCaps * roscompressedimagesrc_getcaps(GstBaseSrc * base_src, GstCaps * filter);

static void roscompressedimagesrc_sub_cb(
  Roscompressedimagesrc * src,
  sensor_msgs::msg::CompressedImage::ConstSharedPtr msg);
static sensor_msgs::msg::CompressedImage::ConstSharedPtr roscompressedimagesrc_wait_for_msg(
  Roscompressedimagesrc * src);

enum {
  PROP_0,
  PROP_ROS_TOPIC,
  PROP_ROS_FRAME_ID,
  PROP_ROS_ENCODING,
};

/* pad templates */

static GstStaticPadTemplate roscompressedimagesrc_src_template = GST_STATIC_PAD_TEMPLATE(
  "src", GST_PAD_SRC, GST_PAD_ALWAYS,
  GST_STATIC_CAPS(ROS_COMPRESSED_IMAGE_MSG_CAPS));

/* class initialization */

G_DEFINE_TYPE_WITH_CODE(
  Roscompressedimagesrc, roscompressedimagesrc, GST_TYPE_ROS_BASE_SRC,
  GST_DEBUG_CATEGORY_INIT(
    roscompressedimagesrc_debug_category, "roscompressedimagesrc", 0,
    "debug category for roscompressedimagesrc element"))

static void roscompressedimagesrc_class_init(RoscompressedimagesrcClass * klass)
{
  GObjectClass * object_class = G_OBJECT_CLASS(klass);
  GstElementClass * element_class = GST_ELEMENT_CLASS(klass);
  GstBaseSrcClass * basesrc_class = GST_BASE_SRC_CLASS(klass);
  RosBaseSrcClass * ros_base_src_class = GST_ROS_BASE_SRC_CLASS(klass);

  object_class->set_property = roscompressedimagesrc_set_property;
  object_class->get_property = roscompressedimagesrc_get_property;

  gst_element_class_add_static_pad_template(element_class, &roscompressedimagesrc_src_template);

  gst_element_class_set_static_metadata(
    element_class, "roscompressedimagesrc", "Source/Video",
    "a gstreamer source that transports ROS CompressedImage msgs over gstreamer",
    "BrettRD <brettrd@brettrd.com>");

  g_object_class_install_property(
    object_class, PROP_ROS_TOPIC,
    g_param_spec_string(
      "ros-topic", "sub-topic", "ROS topic to subscribe to", "gst_compressed_image_sub",
      (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property(
    object_class, PROP_ROS_FRAME_ID,
    g_param_spec_string(
      "ros-frame-id", "frame-id", "frame_id of the compressed image message", "",
      (GParamFlags)(G_PARAM_READABLE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property(
    object_class, PROP_ROS_ENCODING,
    g_param_spec_string(
      "ros-encoding", "encoding-string", "CompressedImage format string (e.g. jpeg, h264, h265)",
      "", (GParamFlags)(G_PARAM_READABLE | G_PARAM_STATIC_STRINGS)));

  ros_base_src_class->open = GST_DEBUG_FUNCPTR(roscompressedimagesrc_open);
  ros_base_src_class->close = GST_DEBUG_FUNCPTR(roscompressedimagesrc_close);
  ros_base_src_class->notify_thread = GST_DEBUG_FUNCPTR(roscompressedimagesrc_notify_thread);

  basesrc_class->create = GST_DEBUG_FUNCPTR(roscompressedimagesrc_create);
  basesrc_class->get_caps = GST_DEBUG_FUNCPTR(roscompressedimagesrc_getcaps);
  basesrc_class->query = GST_DEBUG_FUNCPTR(roscompressedimagesrc_query);
}

static void roscompressedimagesrc_init(Roscompressedimagesrc * src)
{
  RosBaseSrc * ros_base_src = GST_ROS_BASE_SRC(src);
  ros_base_src->node_name = g_strdup("gst_compressed_image_src_node");
  src->sub_topic = g_strdup("gst_compressed_image_sub");
  src->frame_id = g_strdup("");
  src->ros_format = g_strdup("");

  src->msg_init = true;
  src->msg_queue_max = 1;
  src->msg_queue_stop = false;
  src->msg_queue = std::deque<sensor_msgs::msg::CompressedImage::ConstSharedPtr>();

  gst_base_src_set_live(GST_BASE_SRC(src), TRUE);
  gst_base_src_set_format(GST_BASE_SRC(src), GST_FORMAT_TIME);
  gst_base_src_set_do_timestamp(GST_BASE_SRC(src), FALSE);
}

static void roscompressedimagesrc_set_property(
  GObject * object, guint property_id, const GValue * value, GParamSpec * pspec)
{
  RosBaseSrc * ros_base_src = GST_ROS_BASE_SRC(object);
  Roscompressedimagesrc * src = GST_ROSCOMPRESSEDIMAGESRC(object);

  GST_DEBUG_OBJECT(src, "set_property");

  switch (property_id) {
    case PROP_ROS_TOPIC:
      if (ros_base_src->node_if) {
        RCLCPP_ERROR(
          ros_base_src->node_if->logging->get_logger(), "can't change topic name once opened");
      } else {
        g_free(src->sub_topic);
        src->sub_topic = g_value_dup_string(value);
      }
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
      break;
  }
}

static void roscompressedimagesrc_get_property(
  GObject * object, guint property_id, GValue * value, GParamSpec * pspec)
{
  Roscompressedimagesrc * src = GST_ROSCOMPRESSEDIMAGESRC(object);

  GST_DEBUG_OBJECT(src, "get_property");
  switch (property_id) {
    case PROP_ROS_TOPIC:
      g_value_set_string(value, src->sub_topic);
      break;

    case PROP_ROS_FRAME_ID:
      g_value_set_string(value, src->frame_id);
      break;

    case PROP_ROS_ENCODING:
      g_value_set_string(value, src->ros_format);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
      break;
  }
}

static gboolean roscompressedimagesrc_open(RosBaseSrc * ros_base_src)
{
  Roscompressedimagesrc * src = GST_ROSCOMPRESSEDIMAGESRC(ros_base_src);

  GST_DEBUG_OBJECT(src, "open");

  src->msg_queue_stop = false;

  auto cb = [src](sensor_msgs::msg::CompressedImage::ConstSharedPtr msg) {
    roscompressedimagesrc_sub_cb(src, msg);
  };
  rclcpp::QoS qos = rclcpp::QoS(1).reliable().durability_volatile();  // reliable, volatile, keep_last=1 - required for image_republisher compatibility in ROS2

  src->sub = rclcpp::create_subscription<sensor_msgs::msg::CompressedImage>(
    ros_base_src->node_if->parameters, ros_base_src->node_if->topics, src->sub_topic, qos, cb);

  return TRUE;
}

static gboolean roscompressedimagesrc_close(RosBaseSrc * ros_base_src)
{
  Roscompressedimagesrc * src = GST_ROSCOMPRESSEDIMAGESRC(ros_base_src);

  GST_DEBUG_OBJECT(src, "close");

  src->sub.reset();

  {
    std::unique_lock<std::mutex> lck(src->msg_queue_mtx);
    src->msg_queue.clear();
    src->msg_queue_stop = true;
  }
  src->msg_queue_cv.notify_all();

  return TRUE;
}

static gboolean roscompressedimagesrc_notify_thread(RosBaseSrc * ros_base_src)
{
  Roscompressedimagesrc * src = GST_ROSCOMPRESSEDIMAGESRC(ros_base_src);

  GST_DEBUG_OBJECT(src, "notify_thread");

  {
    std::unique_lock<std::mutex> lck(src->msg_queue_mtx);
    src->msg_queue_stop = true;
  }
  src->msg_queue_cv.notify_all();

  return TRUE;
}

static GstCaps * roscompressedimagesrc_getcaps(GstBaseSrc * base_src, GstCaps * filter)
{
  RosBaseSrc * ros_base_src = GST_ROS_BASE_SRC(base_src);
  Roscompressedimagesrc * src = GST_ROSCOMPRESSEDIMAGESRC(base_src);

  GST_DEBUG_OBJECT(src, "getcaps");

  if (!ros_base_src->node_if) {
    GST_DEBUG_OBJECT(src, "getcaps with node not ready, returning template");
    return gst_pad_get_pad_template_caps(GST_BASE_SRC(src)->srcpad);
  }

  RCLCPP_INFO(ros_base_src->node_if->logging->get_logger(), "waiting for first message");
  roscompressedimagesrc_wait_for_msg(src);

  if (src->msg_init) {
    GST_DEBUG_OBJECT(src, "getcaps with message not rx'd, returning template");
    return gst_pad_get_pad_template_caps(GST_BASE_SRC(src)->srcpad);
  }

  GstCaps * caps = NULL;
  if (0 == g_strcmp0(src->ros_format, "jpeg")) {
    caps = gst_caps_from_string("image/jpeg");
  } else if (0 == g_strcmp0(src->ros_format, "h264")) {
    caps = gst_caps_from_string(
      "video/x-h264, stream-format=(string)byte-stream, alignment=(string)au");
  } else if (0 == g_strcmp0(src->ros_format, "h265")) {
    caps = gst_caps_from_string(
      "video/x-h265, stream-format=(string)byte-stream, alignment=(string)au");
  } else {
    GST_WARNING_OBJECT(src, "unknown ros_format '%s', returning template", src->ros_format);
    return gst_pad_get_pad_template_caps(GST_BASE_SRC(src)->srcpad);
  }

  GST_DEBUG_OBJECT(src, "getcaps returning %s", gst_caps_to_string(caps));
  return caps;
}

static gboolean roscompressedimagesrc_query(GstBaseSrc * base_src, GstQuery * query)
{
  gboolean ret;

  switch (GST_QUERY_TYPE(query)) {
    case GST_QUERY_SCHEDULING: {
      gst_query_set_scheduling(query, GST_SCHEDULING_FLAG_SEQUENTIAL, 1, -1, 0);
      gst_query_add_scheduling_mode(query, GST_PAD_MODE_PUSH);
      ret = TRUE;
      break;
    }
    default:
      ret = GST_BASE_SRC_CLASS(roscompressedimagesrc_parent_class)->query(base_src, query);
      break;
  }
  return ret;
}

static GstFlowReturn roscompressedimagesrc_create(
  GstBaseSrc * base_src, guint64 offset, guint size, GstBuffer ** buf)
{
  RosBaseSrc * ros_base_src = GST_ROS_BASE_SRC(base_src);
  Roscompressedimagesrc * src = GST_ROSCOMPRESSEDIMAGESRC(base_src);

  GstMapInfo info;
  size_t length;
  GstFlowReturn ret = GST_FLOW_OK;
  GstBuffer * res_buf;

  GST_DEBUG_OBJECT(src, "create");

  // create() is only called when PLAYING - clear any stop flag left from a previous pause
  {
    std::unique_lock<std::mutex> lck(src->msg_queue_mtx);
    src->msg_queue_stop = false;
  }

  auto msg = roscompressedimagesrc_wait_for_msg(src);
  if (!msg) {
    GST_DEBUG_OBJECT(src, "no message to create buffer from");
    return GST_FLOW_ERROR;
  } else {
    std::unique_lock<std::mutex> lck(src->msg_queue_mtx);
    src->msg_queue.clear();
  }

  length = msg->data.size();
  if (*buf == NULL) {
    ret = GST_BASE_SRC_CLASS(roscompressedimagesrc_parent_class)
            ->alloc(base_src, offset, length, &res_buf);
    if (G_UNLIKELY(ret != GST_FLOW_OK))
      GST_DEBUG_OBJECT(src, "Failed to allocate buffer of %lu bytes", length);
    *buf = res_buf;
    size = length;
  } else {
    res_buf = *buf;
  }

  if (length != size) GST_DEBUG_OBJECT(src, "size mismatch, %ld, %d", length, size);

  gst_buffer_map(*buf, &info, GST_MAP_READ);
  info.size = length;
  memcpy(info.data, msg->data.data(), length);
  gst_buffer_unmap(*buf, &info);

  GST_BUFFER_PTS(*buf) = rclcpp::Time(msg->header.stamp).nanoseconds();

  return ret;
}

static void roscompressedimagesrc_sub_cb(
  Roscompressedimagesrc * src,
  sensor_msgs::msg::CompressedImage::ConstSharedPtr msg)
{
  RosBaseSrc * ros_base_src = GST_ROS_BASE_SRC(src);

  if (src->msg_init) {
    g_free(src->ros_format);
    src->ros_format = g_strdup(msg->format.c_str());

    g_free(src->frame_id);
    src->frame_id = g_strdup(msg->header.frame_id.c_str());

    src->msg_init = false;
    RCLCPP_INFO(
      ros_base_src->node_if->logging->get_logger(),
      "first compressed image message received, format: %s", src->ros_format);
  }

  std::unique_lock<std::mutex> lck(src->msg_queue_mtx);
  src->msg_queue.push_front(msg);
  while (src->msg_queue.size() > src->msg_queue_max) {
    src->msg_queue.pop_back();  // drop oldest, keep newest
    RCLCPP_WARN(ros_base_src->node_if->logging->get_logger(), "dropping message");
  }
  src->msg_queue_cv.notify_one();
}

static sensor_msgs::msg::CompressedImage::ConstSharedPtr roscompressedimagesrc_wait_for_msg(
  Roscompressedimagesrc * src)
{
  std::unique_lock<std::mutex> lck(src->msg_queue_mtx);
  src->msg_queue_cv.wait(lck, [src] {
    return !src->msg_queue.empty() || src->msg_queue_stop;
  });
  if (src->msg_queue.empty()) {
    return sensor_msgs::msg::CompressedImage::ConstSharedPtr();
  }
  return src->msg_queue.front();
}
