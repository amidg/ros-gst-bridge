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

#ifndef _GST_ROSCOMPRESSEDIMAGESRC_H_
#define _GST_ROSCOMPRESSEDIMAGESRC_H_

#include <gst/base/gstbasesrc.h>
#include <gst_bridge/gst_bridge.h>
#include <gst_bridge/rosbasesrc.h>

#include <condition_variable>
#include <deque>
#include <mutex>
#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/compressed_image.hpp>

G_BEGIN_DECLS

#define GST_TYPE_ROSCOMPRESSEDIMAGESRC (roscompressedimagesrc_get_type())
#define GST_ROSCOMPRESSEDIMAGESRC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_ROSCOMPRESSEDIMAGESRC, Roscompressedimagesrc))
#define GST_ROSCOMPRESSEDIMAGESRC_CAST(obj) ((Roscompressedimagesrc *)obj)
#define GST_ROSCOMPRESSEDIMAGESRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_ROSCOMPRESSEDIMAGESRC, RoscompressedimagesrcClass))
#define GST_ROSCOMPRESSEDIMAGESRC_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS((obj), GST_TYPE_ROSCOMPRESSEDIMAGESRC, RoscompressedimagesrcClass))
#define GST_IS_ROSCOMPRESSEDIMAGESRC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_ROSCOMPRESSEDIMAGESRC))
#define GST_IS_ROSCOMPRESSEDIMAGESRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_ROSCOMPRESSEDIMAGESRC))

typedef struct _Roscompressedimagesrc Roscompressedimagesrc;
typedef struct _RoscompressedimagesrcClass RoscompressedimagesrcClass;

struct _Roscompressedimagesrc
{
  RosBaseSrc parent;
  gchar * sub_topic;
  gchar * frame_id;
  gchar * ros_format;  // CompressedImage.format from first msg (e.g., "jpeg", "h264")

  bool msg_init;

  size_t msg_queue_max;
  std::deque<sensor_msgs::msg::CompressedImage::ConstSharedPtr> msg_queue;
  std::mutex msg_queue_mtx;
  std::condition_variable msg_queue_cv;

  rclcpp::Subscription<sensor_msgs::msg::CompressedImage>::SharedPtr sub;
};

struct _RoscompressedimagesrcClass
{
  RosBaseSrcClass parent_class;
};

GType roscompressedimagesrc_get_type(void);

G_END_DECLS

#endif
