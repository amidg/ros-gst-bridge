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

#ifndef _GST_ROSCOMPRESSEDIMAGESINK_H_
#define _GST_ROSCOMPRESSEDIMAGESINK_H_

#include <gst/base/gstbasesink.h>
#include <gst_bridge/gst_bridge.h>
#include <gst_bridge/rosbasesink.h>

#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/compressed_image.hpp>

G_BEGIN_DECLS

#define GST_TYPE_ROSCOMPRESSEDIMAGESINK (roscompressedimagesink_get_type())
#define GST_ROSCOMPRESSEDIMAGESINK(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_ROSCOMPRESSEDIMAGESINK, Roscompressedimagesink))
#define GST_ROSCOMPRESSEDIMAGESINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_ROSCOMPRESSEDIMAGESINK, RoscompressedimagesinkClass))
#define GST_IS_ROSCOMPRESSEDIMAGESINK(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_ROSCOMPRESSEDIMAGESINK))
#define GST_IS_ROSCOMPRESSEDIMAGESINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_ROSCOMPRESSEDIMAGESINK))

typedef struct _Roscompressedimagesink Roscompressedimagesink;
typedef struct _RoscompressedimagesinkClass RoscompressedimagesinkClass;

struct _Roscompressedimagesink
{
  RosBaseSink parent;

  gchar * pub_topic;
  gchar * frame_id;
  gchar * ros_format;  // "jpeg", "h264", or "h265" — set from setcaps or property override

  rclcpp::Publisher<sensor_msgs::msg::CompressedImage>::SharedPtr pub;
};

struct _RoscompressedimagesinkClass
{
  RosBaseSinkClass parent_class;
};

GType roscompressedimagesink_get_type(void);

G_END_DECLS

#endif
