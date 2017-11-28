#ifndef VSB_SEMESTRAL_PROJECT_MATCHER_H
#define VSB_SEMESTRAL_PROJECT_MATCHER_H

#include <opencv2/core/hal/interface.h>
#include <opencv2/core/mat.hpp>
#include <memory>
#include "../core/window.h"
#include "../core/match.h"
#include "../core/value_point.h"
#include "../core/classifier_criteria.h"

namespace tless {
    /**
     * class Hasher
     *
     * Used to train HashTables and quickly verify what templates should be matched
     * per each window passed form objectness detection
     */
    class Matcher {
    private:
        cv::Ptr<ClassifierCriteria> criteria;

        // Methods
        cv::Vec3b normalizeHSV(cv::Vec3b &hsv);
        void extractFeatures(std::vector<Template> &templates);
        void generateFeaturePoints(std::vector<Template> &templates);
        void cherryPickFeaturePoints(std::vector<ValuePoint<float>> &points, double tMinDistance, uint pointsCount, std::vector<cv::Point> &out);
        void nonMaximaSuppression(std::vector<Match> &matches);

        // Tests
        int testObjectSize(float scale, float depth, Window &window, cv::Mat &sceneDepth, cv::Point &stable); // Test I
        int testSurfaceNormal(uchar normal, Window &window, cv::Mat &sceneSurfaceNormalsQuantized, cv::Point &stable); // Test II
        int testGradients(uchar gradient, Window &window, cv::Mat &sceneAnglesQuantized, cv::Mat &sceneMagnitudes, cv::Point &edge); // Test III
        int testDepth(float scale, float diameter, float depthMedian, Window &window, cv::Mat &sceneDepth, cv::Point &stable); // Test IV
        int testColor(cv::Vec3b HSV, Window &window, cv::Mat &sceneHSV, cv::Point &stable); // Test V
    public:
        // Constructor
        Matcher(cv::Ptr<ClassifierCriteria> criteria) : criteria(criteria) {}

        // Methods
        void match(float scale, cv::Mat &sceneHSV, cv::Mat &sceneDepth, cv::Mat &sceneMagnitudes, cv::Mat &sceneAnglesQuantized,
                   cv::Mat &sceneSurfaceNormalsQuantized, std::vector<Window> &windows, std::vector<Match> &matches);
        void train(std::vector<Template> &templates);
    };
}

#endif
