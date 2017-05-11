/*!
  @file   drishti/hci/FaceFinder.h
  @author David Hirvonen
  @brief  Face detection and tracking class with GPU acceleration.

  \copyright Copyright 2014-2016 Elucideye, Inc. All rights reserved.
  \license{This project is released under the 3 Clause BSD License.}

*/

#ifndef __drishti_hci_FaceFinder_h__
#define __drishti_hci_FaceFinder_h__

#include "drishti/hci/drishti_hci.h"
#include "drishti/hci/Scene.hpp"
#include "drishti/hci/FaceMonitor.h"
#include "drishti/acf/GPUACF.h"
#include "drishti/acf/ACF.h"
#include "drishti/face/Face.h"
#include "drishti/face/FaceDetectorFactory.h"
#include "drishti/sensor/Sensor.h"

#include "ogles_gpgpu/common/proc/flow.h"

#include "thread_pool/thread_pool.hpp"

#include <future>
#include <memory>
#include <chrono>
#include <functional>
#include <deque>

#define DRISHTI_HCI_FACEFINDER_INTERVAL 0.1
#define DRISHTI_HCI_FACEFINDER_DO_ELLIPSO_POLAR 0

// clang-format off
namespace ogles_gpgpu
{
    class SwizzleProc;
    class BlobFilter;
    class FacePainter;
    class FifoProc;
    class TransformProc;
    class EyeFilter;
    class EllipsoPolarWarp;
}

namespace spdlog { class logger; }

namespace drishti
{
    namespace sensor
    {
        class Sensor;
    }
    namespace face
    {
        class FaceModelEstimator;
        class FaceDetector;
    }
}
// clang-format on

DRISHTI_HCI_NAMESPACE_BEGIN

class FaceFinder
{
public:
    using HighResolutionClock = std::chrono::high_resolution_clock;
    using TimePoint = HighResolutionClock::time_point; // <std::chrono::system_clock>;
    using FrameInput = ogles_gpgpu::FrameInput;
    using FeaturePoints = std::vector<FeaturePoint>;
    using ImageLogger = std::function<void(const cv::Mat &image)>;

    using FaceDetectorFactoryPtr = std::shared_ptr<drishti::face::FaceDetectorFactory>;

    struct TimerInfo
    {
        double detectionTime = 0.0;
        double regressionTime = 0.0;
        double eyeRegressionTime = 0.0;
        double acfProcessingTime = 0.0;
        double blobExtractionTime = 0.0;
        double renderSceneTime = 0.0;
        std::function<void(double second)> detectionTimeLogger;
        std::function<void(double second)> regressionTimeLogger;
        std::function<void(double second)> eyeRegressionTimeLogger;
        std::function<void(double second)> acfProcessingTimeLogger;
        std::function<void(double second)> blobExtractionTimeLogger;
        std::function<void(double second)> renderSceneTimeLogger;

        friend std::ostream& operator<<(std::ostream& stream, const TimerInfo& info);
    };

    struct Settings
    {
        std::shared_ptr<drishti::sensor::SensorModel> sensor;
        std::shared_ptr<spdlog::logger> logger;
        std::shared_ptr<tp::ThreadPool<>> threads;
        ImageLogger imageLogger;
        int outputOrientation = 0;
        int frameDelay = 1;
        bool doLandmarks = true;
        bool doFlow = true;
        bool doBlobs = false;
        float minDetectionDistance = 0.f;
        float maxDetectionDistance = 1.f;
    };

    FaceFinder(FaceDetectorFactoryPtr& factory, Settings& config, void* glContext = nullptr);
    ~FaceFinder();

    virtual void initialize(); // must call at startup

    static std::unique_ptr<FaceFinder> create(FaceDetectorFactoryPtr& factory, Settings& config, void* glContext = nullptr);

    virtual GLuint operator()(const FrameInput& frame);

    float getMaxDistance() const;
    float getMinDistance() const;

    void setDoCpuAcf(bool flag);
    bool getDoCpuAcf() const;

    void setFaceFinderInterval(double interval);
    double getFaceFinderInterval() const;

    void setBrightness(float value)
    {
        m_brightness = value;
    }

    void registerFaceMonitorCallback(FaceMonitor* callback);

    virtual bool doAnnotations() const
    {
        return m_doAnnotations;
    }
    
    void setImageLogger(const ImageLogger &logger)
    {
        m_imageLogger = logger;
    }

protected:
    
    bool needsDetection(const TimePoint& ts) const;

    void computeGazePoints();
    void updateEyes(GLuint inputTexId, const ScenePrimitives& scene);

    void notifyListeners(const ScenePrimitives& scene, const TimePoint& time, bool isFull);
    bool hasValidFaceRequest(const ScenePrimitives& scene, const TimePoint& time) const;

    virtual void init(const cv::Size& inputSize);
    virtual void initPainter(const cv::Size& inputSizeUp);

    void initACF(const cv::Size& inputSizeUp);
    void initFIFO(const cv::Size& inputSize, std::size_t n);
    void initBlobFilter();
    void initColormap(); // [0..359];
    void initEyeEnhancer(const cv::Size& inputSizeUp, const cv::Size& eyesSize);
    void initIris(const cv::Size& size);

    void initTimeLoggers();
    void init2(drishti::face::FaceDetectorFactory& resources);
    void detect2(const FrameInput& frame, ScenePrimitives& scene);

    void dumpEyes(std::vector<cv::Mat4b>& frames, std::vector<std::array<eye::EyeModel, 2>>& eyes);
    void dumpFaces(std::vector<cv::Mat4b>& frames);
    virtual int detect(const FrameInput& frame, ScenePrimitives& scene, bool doDetection);
    virtual GLuint paint(const ScenePrimitives& scene, GLuint inputTexture);
    virtual void preprocess(const FrameInput& frame, ScenePrimitives& scene, bool needsDetection); // compute acf
    int computeDetectionWidth(const cv::Size& inputSizeUp) const;

    void computeAcf(const FrameInput& frame, bool doLuv, bool doDetection);
    std::shared_ptr<acf::Detector::Pyramid> createAcfGpu(const FrameInput& frame, bool doDetection);
    std::shared_ptr<acf::Detector::Pyramid> createAcfCpu(const FrameInput& frame, bool doDetection);
    void fill(drishti::acf::Detector::Pyramid& P);

    cv::Mat3f m_colors32FC3; // map angles to colors

    void* m_glContext = nullptr;
    bool m_hasInit = false;
    float m_ACFScale = 2.0f;
    int m_outputOrientation = 0;

    float m_brightness = 1.f;

    uint64_t m_frameIndex = 0;

    using time_point = std::chrono::high_resolution_clock::time_point;
    std::pair<time_point, std::vector<cv::Rect>> m_objects;

    drishti::acf::Detector::Pyramid m_P;

    bool m_debugACF = false;

    double m_faceFinderInterval = DRISHTI_HCI_FACEFINDER_INTERVAL;

    float m_minDistanceMeters = 0.f;
    float m_maxDistanceMeters = 10.0f;

    bool m_doLandmarks = false;
    int m_landmarksWidth = 256;

    bool m_doFlow = false;
    int m_flowWidth = 256;

    bool m_doBlobs = false;

    bool m_doIris = false;

    bool m_doCpuACF = false;

    bool m_doEyeFlow = false;

    float m_regressorCropScale = 0.f;

    cv::Size m_eyesSize = { 480, 240 };

    std::vector<cv::Size> m_pyramidSizes;

    bool m_doNMSGlobal = true;
    drishti::acf::Detector* m_detector = nullptr; // weak ref

    std::shared_ptr<drishti::face::FaceDetector> m_faceDetector;

    // Model estimator from pinhole camera model:
    std::shared_ptr<drishti::face::FaceModelEstimator> m_faceEstimator;

    std::shared_ptr<ogles_gpgpu::ACF> m_acf;
    std::shared_ptr<ogles_gpgpu::FifoProc> m_fifo; // store last N faces
    std::shared_ptr<ogles_gpgpu::FacePainter> m_painter;
    std::shared_ptr<ogles_gpgpu::TransformProc> m_rotater; // For QT
    std::shared_ptr<ogles_gpgpu::BlobFilter> m_blobFilter;
    std::shared_ptr<ogles_gpgpu::EyeFilter> m_eyeFilter;
    std::shared_ptr<ogles_gpgpu::EllipsoPolarWarp> m_ellipsoPolar[2];

    std::shared_ptr<ogles_gpgpu::FlowOptPipeline> m_eyeFlow;
    std::shared_ptr<ogles_gpgpu::SwizzleProc> m_eyeFlowBgra; // (optional)
    ogles_gpgpu::ProcInterface* m_eyeFlowBgraInterface = nullptr;

    int m_index = 0;
    std::future<ScenePrimitives> m_scene;
    std::deque<ScenePrimitives> m_scenePrimitives; // stash
    cv::Point3f m_faceMotion;

    FeaturePoints m_gazePoints;
    std::array<FeaturePoints, 2> m_eyePoints;

    cv::Point2f m_eyeMotion;
    std::vector<cv::Vec4f> m_eyeFlowField;

    TimerInfo m_timerInfo;

    std::shared_ptr<drishti::face::FaceDetectorFactory> m_factory;
    std::shared_ptr<drishti::sensor::SensorModel> m_sensor;
    std::shared_ptr<spdlog::logger> m_logger;
    std::shared_ptr<tp::ThreadPool<>> m_threads;
    ImageLogger m_imageLogger;

    TimePoint m_start;

    std::vector<FaceMonitor*> m_faceMonitorCallback;

    bool m_doAnnotations = true;
    std::mutex m_mutex;
};

std::ostream& operator<<(std::ostream& stream, const FaceFinder::TimerInfo& info);

DRISHTI_HCI_NAMESPACE_END

#endif // __drishti_hci_FaceFinder_h__
