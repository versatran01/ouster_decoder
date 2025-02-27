#pragma once

#include <sensor_msgs/CameraInfo.h>
#include <sensor_msgs/PointCloud2.h>

#include <opencv2/core/mat.hpp>

#include "ouster_ros/ros.h"

namespace ouster_decoder {

inline constexpr double Deg2Rad(double deg) { return deg * M_PI / 180.0; }

/// @brief image data in scan
struct ImageData {
  float x{};
  float y{};
  float z{};
  uint16_t range_raw{};
  uint16_t signal_raw{};

  static constexpr auto kMaxUint16 = std::numeric_limits<uint16_t>::max();

  void set_range(double range, double scale) noexcept {
    range_raw = std::min(range * scale, static_cast<double>(kMaxUint16));
  }

  void set_signal(double signal) noexcept {
    signal_raw = std::min(signal, static_cast<double>(kMaxUint16));
  }
};

static_assert(sizeof(ImageData) == sizeof(float) * 4,
              "Size of ImageData must be 4 floats (16 bytes)");

/// @brief Stores SensorInfo from ouster with some other useful data
struct LidarModel {
  LidarModel() = default;
  explicit LidarModel(const std::string& metadata);

  /// @brief whether this model is ready
  bool Initialized() const { return !altitudes.empty(); }

  int rows{};                     // number of beams
  int cols{};                     // cols of a full scan
  int freq{};                     // frequency
  double dt_col{};                // delta time between two columns [s]
  double dt_packet{};             // delta time between two packets [s]
  double d_azimuth{};             // delta angle between two columns [rad]
  double beam_offset{};           // distance between beam to origin
  std::vector<double> altitudes;  // altitude angles, high to low [rad]
  std::vector<double> azimuths;   // azimuths offset angles [rad]
  ouster_ros::sensor::sensor_info info;                  // sensor info
  ouster_ros::sensor::packet_format const* pf{nullptr};  // packet format

  const auto& pixel_shifts() const noexcept {
    return info.format.pixel_shift_by_row;
  }

  /// @brief Convert lidar range data to xyz
  /// @details see software manual 3.1.2 Lidar Range to XYZ
  ///
  ///    y    r
  ///    ^   / -> rotate clockwise
  ///    |  /
  ///    | /
  ///    |/  theta
  ///    o ---------> x  (connector)
  ///
  Eigen::Vector3f ToPoint(float range, float theta_enc, int row) const;

  /// @brief Return a unique id for a measurement
  int Uid(int fid, int mid) const noexcept { return fid * cols + mid; }

  /// @brief Update camera info with this model
  void UpdateCameraInfo(sensor_msgs::CameraInfo& cinfo) const;
};

/// @brief Stores data for a (sub)scan
struct LidarScan {
  int icol{0};   // column index
  int iscan{0};  // subscan index
  int prev_uid{-1};
  int num_valid{0};
  double min_range{};
  double max_range{};
  double range_scale{};
  bool destagger{false};

  cv::Mat image;
  sensor_msgs::PointCloud2 cloud;
  std::vector<uint64_t> times;  // all time stamps [nanosecond]

  float* CloudPtr(int r, int c) {
    const auto i = r * cloud.width + c;
    return reinterpret_cast<float*>(cloud.data.data() + i * cloud.point_step);
  }

  int rows() const noexcept { return image.rows; }
  int cols() const noexcept { return image.cols; }

  /// @brief whether this scan is full
  bool IsFull() const noexcept { return icol >= cols(); }

  /// @brief Starting column of this scan
  int StartingCol() const noexcept { return iscan * cols(); }

  /// @brief Detect if there is a jump in the lidar data
  /// @return 0 - no jump, >0 - jump forward in time, <0 - jump backward in time
  int DetectJump(int uid) noexcept;

  /// @brief Allocate storage for the scan
  void Allocate(int rows, int cols);

  /// @brief Hard reset internal counters and prev_uid
  void HardReset() noexcept;

  /// @brief Try to reset the internal counters if it is full
  void SoftReset(int full_col) noexcept;

  /// @brief Invalidate an entire column
  void InvalidateColumn(double dt_col);

  /// @brief Decode column
  void DecodeColumn(const uint8_t* const col_buf, const LidarModel& model);

  /// @brief Update camera info roi data with this scan
  void UpdateCinfo(sensor_msgs::CameraInfo& cinfo) const noexcept;
};

std::vector<sensor_msgs::PointField> MakePointFieldsXYZI() noexcept;

}  // namespace ouster_decoder
