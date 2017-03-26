#include <unordered_set>
#include "hasher.h"
#include "matching.h"

const int Hasher::IMG_16BIT_VALUE_MAX = 65535; // <0, 65535> => 65536 values
const int Hasher::IMG_16BIT_VALUES_RANGE = (IMG_16BIT_VALUE_MAX * 2) + 1; // <-65535, 65535> => 131071 values + (one zero)

cv::Vec3d Hasher::extractSurfaceNormal(const cv::Mat &src, const cv::Point c) {
    // Checks
    assert(!src.empty());

    float dzdx = (src.at<float>(c.y, c.x + 1) - src.at<float>(c.y, c.x - 1)) / 2.0f;
    float dzdy = (src.at<float>(c.y + 1, c.x) - src.at<float>(c.y - 1, c.x)) / 2.0f;
    cv::Vec3f d(-dzdy, -dzdx, 1.0f);

    return cv::normalize(d);
}

cv::Vec2i Hasher::extractRelativeDepths(const cv::Mat &src, const cv::Point p1, const cv::Point p2, const cv::Point p3) {
    return cv::Vec2i(
        static_cast<int>(src.at<float>(p2) - src.at<float>(p1)),
        static_cast<int>(src.at<float>(p3) - src.at<float>(p1))
    );
}

int Hasher::quantizeSurfaceNormals(cv::Vec3f normal) {
    // Normal z coordinate should not be > 0
    assert(normal[2] >= 0);

    // For quantization of surface normals into 8 bins
    // in our case z is always positive, that's why we're using
    // 8 octants in top half of sphere only
    cv::Vec3f octantsNormals[8] = {
        cv::Vec3f(1.0f, 0, 1.0f), // 0. octant
        cv::Vec3f(1.0f, 1.0f, 1.0f), // 1. octant
        cv::Vec3f(0, 1.0f, 1.0f), // 2. octant
        cv::Vec3f(-1.0f, 1.0f, 1.0f), // 3. octant
        cv::Vec3f(-1.0f, 0.0f, 1.0f), // 4. octant
        cv::Vec3f(-1.0f, -1.0f, 1.0f), // 5. octant
        cv::Vec3f(0, -1.0f, 1.0f), // 6. octant
        cv::Vec3f(1.0f, -1.0f, 1.0f), // 7. octant
    };

    int minIndex = -1;
    float maxDot = 0, dot;
    for (int i = 0; i < 8; i++) {
        // By doing dot product between octant normals and calculated normal
        // we can find maximum -> index of octant where the vector belongs to
        cv::Vec3f octaNormal = cv::normalize(octantsNormals[i]);
        dot = normal.dot(octaNormal);

        if (dot > maxDot) {
            maxDot = dot;
            minIndex = i;
        }
    }

    // Index should in interval <0,7>
    assert(minIndex >= 0 && minIndex < 8);

    return minIndex;
}

int Hasher::quantizeDepths(float depth) {
    // Depth should have max value of <-65536, +65536>
    assert(depth >= -IMG_16BIT_VALUE_MAX && depth <= IMG_16BIT_VALUE_MAX);
    assert(histogramBinRanges.size() > 0);

    // Loop through histogram ranges and return quantized index
    for (int i = 0; i < histogramBinRanges.size(); i++) {
        if (histogramBinRanges[i].start >= depth && depth < histogramBinRanges[i].end) {
            return i;
        }
    }

    // If value is IMG_16BIT_VALUE_MAX it belongs to last bin
    return (int) histogramBinRanges.size() - 1;
}

void Hasher::generateTriplets(std::vector<HashTable> &hashTables) {
    // Generate triplets
    for (int i = 0; i < hashTableCount; ++i) {
        HashTable h(Triplet::createRandomTriplet(featurePointsGrid));
        hashTables.push_back(h);
    }

    // TODO - joint entropy of 5k templates, instead of 100 templates
    // Check for unique triplets, and regenerate duplicates
    bool duplicate;
    do {
        duplicate = false;
        for (int i = 0; i < hashTables.size(); ++i) {
            for (int j = 0; j < hashTables.size(); ++j) {
                // Don't compare same triplets
                if (i == j) continue;
                if (hashTables[i].triplet == hashTables[j].triplet) {
                    // Duplicate generate new triplet
                    duplicate = true;
                    hashTables[j].triplet = Triplet::createRandomTriplet(featurePointsGrid);
                }
            }
        }
    } while (duplicate);
}

void Hasher::calculateDepthHistogramRanges(unsigned long histogramSum, unsigned long *histogramValues) {
    // Checks
    assert(histogramSum > 0);

    std::vector<cv::Range> ranges;
    unsigned long tmpBinCount = 0;
    int rangeStart = 0, binsCreated = 0;

    // Calculate approximate size of every bin
    const unsigned long binCount = histogramSum / histogramBinCount;

    // Loop trough histogram and determine ranges
    for (int i = 0; i < IMG_16BIT_VALUES_RANGE; i++) {
        if (histogramValues[i] > 0) {
            tmpBinCount += histogramValues[i];
        }

        // Check if we filled one bin, if yes, save range and reset tmpBinCount
        if (tmpBinCount >= binCount) {
            tmpBinCount = 0;
            ranges.push_back(cv::Range(rangeStart - IMG_16BIT_VALUE_MAX, i - IMG_16BIT_VALUE_MAX));
            rangeStart = i;
            binsCreated++;
            i--;
        }

        // Define last range from current i to end [IMG_16BIT_VALUE_MAX]
        if ((binsCreated + 1) >= histogramBinCount) {
            ranges.push_back(cv::Range(i - IMG_16BIT_VALUE_MAX + 1, IMG_16BIT_VALUE_MAX));
            break;
        }
    }

#ifndef NDEBUG
    // Print results
    std::cout << "DONE! Approximate " << binCount << " values per bin" << std::endl;
    for (int i = 0; i < ranges.size(); i++) {
        std::cout << "       |_ " << i << ". <" << ranges[i].start << ", " << ranges[i].end << (i + 1 == ranges.size() ? ">" : ")") << std::endl;
    }
#endif

    // Set histogram ranges
    setHistogramBinRanges(ranges);
}

void Hasher::calculateDepthBinRanges(const std::vector<TemplateGroup> &groups, std::vector<HashTable> &hashTables) {
    // Histogram values <-65535, +65535> possible values
    unsigned long histogramValues[IMG_16BIT_VALUES_RANGE];
    unsigned long histogramSum = 0;

    // Reset histogram values
    for (int i = 0; i < IMG_16BIT_VALUES_RANGE; i++) {
        histogramValues[i] = 0;
    }

    // Calculate histogram values, using generated triplets and relative depths calculation
    for (auto &hashTable : hashTables) {
        for (auto &group : groups) {
            for (auto &t : group.templates) {
                // Checks
                assert(!t.srcDepth.empty());

                // Get triplet points
                cv::Vec4f coordParams = Triplet::getCoordParams(t.srcDepth.cols, t.srcDepth.rows, featurePointsGrid);
                cv::Point p1 = hashTable.triplet.getP1Coords(coordParams);
                cv::Point p2 = hashTable.triplet.getP2Coords(coordParams);
                cv::Point p3 = hashTable.triplet.getP3Coords(coordParams);

                // Check if we're not out of bounds
                assert(p1.x >= 0 && p1.x < t.srcDepth.cols);
                assert(p1.y >= 0 && p1.y < t.srcDepth.rows);
                assert(p2.x >= 0 && p2.x < t.srcDepth.cols);
                assert(p2.y >= 0 && p2.y < t.srcDepth.rows);
                assert(p3.x >= 0 && p3.x < t.srcDepth.cols);
                assert(p3.y >= 0 && p3.y < t.srcDepth.rows);

                // Relative depths
                cv::Vec2i relativeDepths = extractRelativeDepths(t.srcDepth, p1, p2, p3);

                // Add offset and count given values
                histogramValues[relativeDepths[0] + IMG_16BIT_VALUE_MAX] += 1;
                histogramValues[relativeDepths[1] + IMG_16BIT_VALUE_MAX] += 1;
                histogramSum += 2; // Add 2 to sum of histogram values
            }
        }
    }

    // Calculate ranges from retrieved data
    calculateDepthHistogramRanges(histogramSum, histogramValues);
}

void Hasher::initialize(const std::vector<TemplateGroup> &groups, std::vector<HashTable> &hashTables) {
    // Checks
    assert(groups.size() > 0);
    assert(hashTableCount > 0);
    assert(featurePointsGrid.width > 0);
    assert(featurePointsGrid.height > 0);

    // Init hash tables
    hashTables.reserve(hashTableCount);
    generateTriplets(hashTables);

    // Calculate ranges of depth bins for training
#ifndef NDEBUG
    std::cout << "  |_ Calculating depth bin ranges... ";
#endif
    calculateDepthBinRanges(groups, hashTables);
}

void Hasher::train(const std::vector<TemplateGroup> &groups, std::vector<HashTable> &hashTables) {
    // Prepare hash tables and histogram bin ranges
    initialize(groups, hashTables);

    // Fill hash tables with templates and keys quantizied from measured values
    for (auto &hashTable : hashTables) {
        for (auto &group : groups) {
            for (auto &t : group.templates) {
                // Checks
                assert(!t.srcDepth.empty());

                // Get triplet points
                cv::Vec4f coordParams = Triplet::getCoordParams(t.srcDepth.cols, t.srcDepth.rows, featurePointsGrid);
                cv::Point p1 = hashTable.triplet.getP1Coords(coordParams);
                cv::Point p2 = hashTable.triplet.getP2Coords(coordParams);
                cv::Point p3 = hashTable.triplet.getP3Coords(coordParams);

                // Check if we're not out of bounds
                assert(p1.x >= 0 && p1.x < t.srcDepth.cols);
                assert(p1.y >= 0 && p1.y < t.srcDepth.rows);
                assert(p2.x >= 0 && p2.x < t.srcDepth.cols);
                assert(p2.y >= 0 && p2.y < t.srcDepth.rows);
                assert(p3.x >= 0 && p3.x < t.srcDepth.cols);
                assert(p3.y >= 0 && p3.y < t.srcDepth.rows);

                // Relative depths
                cv::Vec2i relativeDepths = extractRelativeDepths(t.srcDepth, p1, p2, p3);

                // Generate hash key
                HashKey key(
                    quantizeDepths(relativeDepths[0]),
                    quantizeDepths(relativeDepths[1]),
                    quantizeSurfaceNormals(extractSurfaceNormal(t.srcDepth, p1)),
                    quantizeSurfaceNormals(extractSurfaceNormal(t.srcDepth, p2)),
                    quantizeSurfaceNormals(extractSurfaceNormal(t.srcDepth, p3))
                );

                // TODO - maybe can be done better
                // Check if key exists, if not initialize it
                if (hashTable.templates.find(key) == hashTable.templates.end()) {
                    std::vector<Template> hashTemplates;
                    hashTable.templates[key] = hashTemplates;
                }

                // Get reference to entry and check for duplicates, if it passes, insert
                auto &entry = hashTable.templates.at(key);
                if (std::find(entry.begin(), entry.end(), t) == entry.end()) {
                    entry.push_back(t);
                }
            }
        }
    }
}

void Hasher::verifyTemplateCandidates(const cv::Mat &sceneGrayscale, cv::Rect &objectnessROI, std::vector<HashTable> &hashTables, std::vector<TemplateGroup> &groups) {
    // Sliding window - fixed WIDTH
    cv::Size w(120, 120);
    cv::Mat s = sceneGrayscale.clone();
    int step = 5;
    
    // Passed templates
    std::vector<std::vector<Template*>> windowTemplates; // Each window can receive up to N templates
    const int v = 3;
    int templatesPassedCount = 0;

    for (int y = objectnessROI.y; y < (objectnessROI.y + objectnessROI.height) - w.height; y += step) {
        for (int x = objectnessROI.x; x < (objectnessROI.x + objectnessROI.width) - w.width; x += step) {
            s = sceneGrayscale.clone();
            cv::rectangle(s, cv::Point(x, y), cv::Point(x + w.width, y + w.height), cv::Scalar(1.0f));

            // Init templates for this window
            std::vector<Template*> templates;

            for (auto &hashTable : hashTables) {
                // Get triplet points
                cv::Vec4f coordParams = Triplet::getCoordParams(w.width, w.height, featurePointsGrid);
                cv::Point p1 = hashTable.triplet.getP1Coords(coordParams);
                cv::Point p2 = hashTable.triplet.getP2Coords(coordParams);
                cv::Point p3 = hashTable.triplet.getP3Coords(coordParams);

                // Check if we're not out of bounds
                assert(p1.x >= 0 && p1.x < s.cols);
                assert(p1.y >= 0 && p1.y < s.rows);
                assert(p2.x >= 0 && p2.x < s.cols);
                assert(p2.y >= 0 && p2.y < s.rows);
                assert(p3.x >= 0 && p3.x < s.cols);
                assert(p3.y >= 0 && p3.y < s.rows);

                // Relative depths
                cv::Vec2i relativeDepths = extractRelativeDepths(s, p1, p2, p3);

                // Generate hash key
                HashKey key(
                    quantizeDepths(relativeDepths[0]),
                    quantizeDepths(relativeDepths[1]),
                    quantizeSurfaceNormals(extractSurfaceNormal(s, p1)),
                    quantizeSurfaceNormals(extractSurfaceNormal(s, p2)),
                    quantizeSurfaceNormals(extractSurfaceNormal(s, p3))
                );

                // Up votes
                // TODO - we should probably use only up to N templates with highest votes
                for (auto &entry : hashTable.templates[key]) {
                    entry.voteUp();
//                    if (entry.votes >= v) {
//                        // Check if it is not yet in templates
//                        if (std::find(templates.begin(), templates.end(), &entry) == templates.end()) {
//                            templates.push_back(&entry);
//                        }
//                    }
                }
            }

            for (auto &&group : groups) {
                for (auto &&gt : group.templates) {
                    std::cout << gt.votes << std::endl;
                }
            }


            std::cout << "Templates size for current window: " << templates.size() << std::endl;

            // Sort by votes and
            std::sort(templates.begin(), templates.end(), [](const Template* lhs, const Template *rhs) {
                return lhs->votes < rhs->votes;
            });

            for (auto && t : templates) {
                std::cout << t->votes << std::endl;
            }

            // Reset votes for templates for each new sliding window
            for (auto &&t : templates) {
                t->resetVotes();
            }

            // Push filtered templates for this window to new array
            windowTemplates.push_back(templates);
            templatesPassedCount += templates.size();
        }
    }

    // Print filtered size
    std::cout << "Total number of templates passed: " << templatesPassedCount << std::endl;
    std::cout << "Total number of windows: " << windowTemplates.size() << std::endl;

    // Todo pass window locations with template candidates to template matching
}

const cv::Size Hasher::getFeaturePointsGrid() {
    return featurePointsGrid;
}

unsigned int Hasher::getHashTableCount() const {
    return hashTableCount;
}

const std::vector<cv::Range> &Hasher::getHistogramBinRanges() const {
    return histogramBinRanges;
}

void Hasher::setFeaturePointsGrid(cv::Size featurePointsGrid) {
    assert(featurePointsGrid.height > 0 && featurePointsGrid.width > 0);
    this->featurePointsGrid = featurePointsGrid;
}

void Hasher::setHashTableCount(unsigned int hashTableCount) {
    assert(hashTableCount > 0);
    this->hashTableCount = hashTableCount;
}

void Hasher::setHistogramBinRanges(const std::vector<cv::Range> &histogramBinRanges) {
    assert(histogramBinRanges.size() == 5);
    this->histogramBinRanges = histogramBinRanges;
}

unsigned int Hasher::getHistogramBinCount() const {
    return histogramBinCount;
}

void Hasher::setHistogramBinCount(unsigned int histogramBinCount) {
    assert(histogramBinCount > 0);
    this->histogramBinCount = histogramBinCount;
}