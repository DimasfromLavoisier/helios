#include "KDTreeRaycaster.h"
#include "logging.hpp"

#ifdef DATA_ANALYTICS
#include <dataanalytics/HDA_GlobalVars.h>
using helios::analytics::HDA_GV;
#endif

using namespace std;

map<double, Primitive*> KDTreeRaycaster::searchAll(
    glm::dvec3 const rayOrigin,
    glm::dvec3 const rayDir,
    double const tmin,
    double const tmax,
    bool const groundOnly
) {
    // Prepare search
    KDTreeRaycasterSearch search(rayDir, rayOrigin, groundOnly);
	search.rayDirArray.push_back(rayDir.x);
	search.rayDirArray.push_back(rayDir.y);
    search.rayDirArray.push_back(rayDir.z);
	search.rayOriginArray.push_back(rayOrigin.x);
	search.rayOriginArray.push_back(rayOrigin.y);
	search.rayOriginArray.push_back(rayOrigin.z);

	// Do recursive search
	this->searchAll_recursive(this->root.get(), tmin, tmax, search);

	// Handle search output
	return search.collectedPoints;
}

RaySceneIntersection* KDTreeRaycaster::search(
    glm::dvec3 const rayOrigin,
    glm::dvec3 const rayDir,
    double const tmin,
    double const tmax,
    bool const groundOnly
#ifdef DATA_ANALYTICS
   ,std::vector<double> &subraySimRecord,
    bool const isSubray
#endif
){
    // Prepare search
    KDTreeRaycasterSearch search(rayDir, rayOrigin, groundOnly);
	search.rayDirArray.push_back(rayDir.x);
	search.rayDirArray.push_back(rayDir.y);
	search.rayDirArray.push_back(rayDir.z);
	search.rayOriginArray.push_back(rayOrigin.x);
	search.rayOriginArray.push_back(rayOrigin.y);
	search.rayOriginArray.push_back(rayOrigin.z);

	// Do recursive search
#ifdef DATA_ANALYTICS
    std::size_t positiveDirectionCount = 0, negativeDirectionCount = 0;
    std::size_t noSecondHalfCount = 0, noFirstHalfCount = 0;
    std::size_t parallelDirectionCount = 0, bothSidesCount = 0;
    std::size_t bothSidesSecondTryCount = 0;
#endif
	Primitive* prim = this->search_recursive(
	    this->root.get(),
	    tmin-epsilon,
	    tmax+epsilon,
	    search
#ifdef DATA_ANALYTICS
       ,positiveDirectionCount,
        negativeDirectionCount,
        parallelDirectionCount,
        noSecondHalfCount,
        noFirstHalfCount,
        bothSidesCount,
        bothSidesSecondTryCount,
        isSubray
#endif
    );
#ifdef DATA_ANALYTICS
    // Write counts to subray simulation record
    subraySimRecord[11] = (double) positiveDirectionCount;
    subraySimRecord[12] = (double) negativeDirectionCount;
    subraySimRecord[13] = (double) parallelDirectionCount;
    subraySimRecord[14] = (double) noSecondHalfCount;
    subraySimRecord[15] = (double) noFirstHalfCount;
    subraySimRecord[16] = (double) bothSidesCount;
    subraySimRecord[17] = (double) bothSidesSecondTryCount;
#endif

	// Handle search output
	if (prim == nullptr) return nullptr;
	RaySceneIntersection* result = new RaySceneIntersection();
	result->prim = prim;
	result->point = rayOrigin + (rayDir * search.closestHitDistance);
	result->hitDistance = search.closestHitDistance;
	return result;
}

void KDTreeRaycaster::searchAll_recursive(
    LightKDTreeNode* node,
    double const tmin,
    double const tmax,
    KDTreeRaycasterSearch &search
){

	// ######### BEGIN If node is a leaf, perform ray-primitive intersection on all primitives in the leaf's bucket ###########
	if (node->splitAxis == -1) {
		for (auto prim : *node->primitives) {
			vector<double> tMinMax = prim->getRayIntersection(
			    search.rayOrigin, search.rayDir
            );
			if (tMinMax.empty()) {
				logging::DEBUG("searchAll_recursive: tMinMax is empty");
				continue;
			}
			double newDistance = tMinMax[0];

			// NOTE:
			// Checking for tmin <= newDistance <= tmax here is REQUIRED to prevent the following scenario from producing wrong results:

			// Imagine a primitive extending across multiple partitions (i.e. kdtree leaves). Now, if the tree traversal algorithm
			// arrives at a leaf and checks for ray-primitive-intersections *without* the range check, it might detect an
			// intersection with a primitive that intersects with the partition, but the ray-primitive intersection is *outside*
			// of the partition. Traversal would stop and the intersection would be returned without checking if there are
			// *other* intersections (in other leaves, with other primitives) that are *closer* to the ray originWaypoint. If this was
			// the case, the returned intersection would be wrong.

			if(
			    newDistance > 0 &&
			    (newDistance >= tmin-epsilon && newDistance <= tmax+epsilon)
            ){
				if(
				    !search.groundOnly ||
				    (prim->material != nullptr && prim->material->isGround)
                ){
					search.collectedPoints.insert(pair<double, Primitive*>(
					    newDistance,
					    prim)
                    );
				}
			}
		}
	}
	// ######### END If node is a leaf, perform ray-primitive intersection on all primitives in the leaf's bucket ###########

	// ######### BEGIN If node is not a leaf, figure out which child node(s) to traverse next, in which order #############
	else {
		int a = node->splitAxis;
		double thit = numeric_limits<double>::infinity();
		LightKDTreeNode* first = nullptr;
		LightKDTreeNode* second = nullptr;

		// ############ BEGIN Check ray direction to figure out through which sides the ray passes in which order ###########

		// Case 1: Ray goes in positive direction - it passes through the left side first, then through the right:
		if (search.rayDirArray[a] > 0) {
			first = node->left;
			second = node->right;

			thit = (node->splitPos - search.rayOriginArray[a]) /
			    search.rayDirArray[a];
		}
		// Case 2: Ray goes in negative direction - it passes through the right side first, then through the left:
		else if (search.rayDirArray[a] < 0) {
			first = node->right;
			second = node->left;

			thit = (node->splitPos - search.rayOriginArray[a]) /
                search.rayDirArray[a];
		}
		// Case 3: Ray goes parallel to the split plane - it passes through only one side, depending on it's originWaypoint:
		else {
			first = (search.rayOriginArray[a] < node->splitPos) ?
			    node->left : node->right;
			second = (search.rayOriginArray[a] < node->splitPos) ?
			    node->right : node->left;
		}
		// ############ END Check ray direction to figure out thorugh which sides the ray passes in which order ###########

		// ########### BEGIN Check where the ray crosses the split plane to figure out which sides we need to stop into at all ###########

		// thit >= tmax means that the ray crosses the split plane *after it has already left the volume*.
		// In this case, it never enters the second half.
		if (thit >= tmax) {
			searchAll_recursive(first, tmin, tmax, search);
		}

		// thit <= tmin means that the ray crosses the split plane *before it enters the volume*.
		// In this case, it never enters the first half:
		else if (thit <= tmin) {
			searchAll_recursive(second, tmin, tmax, search);
		}

		// Otherwise, the ray crosses the split plane within the volume.
		// This means that it passes through both sides:
		else {
			searchAll_recursive(first, tmin, thit, search);
			searchAll_recursive(second, thit, tmax, search);
		}
		// ########### END Check where the ray crosses the split plane to figure out which sides we need to stop into at all ###########
	}
	// ######### END If node is not a leaf, figure out which child node(s) to traverse next, in which order #############
}

Primitive* KDTreeRaycaster::search_recursive(
    LightKDTreeNode* node,
    double const tmin,
    double const tmax,
    KDTreeRaycasterSearch &search
#ifdef DATA_ANALYTICS
   ,size_t &positiveDirectionCount,
    size_t &negativeDirectionCount,
    size_t &parallelDirectionCount,
    size_t &noSecondHalfCount,
    size_t &noFirstHalfCount,
    size_t &bothSidesCount,
    size_t &bothSidesSecondTryCount,
    bool const isSubray
#endif
) const {
    if(node==nullptr) return nullptr; // Null nodes cannot contain primitives
	Primitive* hitPrim = nullptr;

	// ######### BEGIN If node is a leaf, perform ray-primitive intersection on all primitives in the leaf's bucket ###########
	if (node->splitAxis == -1) {
		for (auto prim : *node->primitives) {
			double const newDistance = prim->getRayIntersectionDistance(
			    search.rayOrigin, search.rayDir
            );

			// NOTE:
			// Checking for tmin <= newDistance <= tmax here is REQUIRED
			// to prevent the following scenario from producing wrong results:

			// Imagine a primitive extending across multiple partitions
			// (i.e. kdtree leaves).
			// Now, if the tree traversal algorithm
			// arrives at a leaf and checks for ray-primitive-intersections
			// *without* the range check, it might detect an intersection
			// with a primitive that intersects with the partition, but
			// the ray-primitive intersection is *outside* of the partition.
			// Traversal would stop and the intersection would be returned
			// without checking if there are *other* intersections
			// (in other leaves, with other primitives) that are *closer* to
			// the ray originWaypoint. If this was the case, the returned intersection
			// would be wrong.
#ifdef DATA_ANALYTICS
            bool const nonNegativeDistance = newDistance > 0;
            bool const closerThanClosest =
                newDistance < search.closestHitDistance;
            bool const tminCheck = newDistance >= tmin;
            bool const tmaxCheck = newDistance <= tmax;
            if(!nonNegativeDistance){  // For any traced ray
                HDA_GV.incrementRaycasterLeafNegativeDistancesCount();
            } else if(!closerThanClosest){
                HDA_GV.incrementRaycasterLeafFurtherThanClosestCount();
            } else if(!tminCheck){
                HDA_GV.incrementRaycasterLeafFailedTminCheckCount();
            }
            else if(!tmaxCheck){
                HDA_GV.incrementRaycasterLeafFailedTmaxCheckCount();
            }
            if(isSubray){  // For any traced ray that is a subray
                if(!nonNegativeDistance){
                    HDA_GV.incrementSubrayLeafNegativeDistancesCount();
                } else if(!closerThanClosest){
                    HDA_GV.incrementSubrayLeafFurtherThanClosestCount();
                } else if(!tminCheck){
                    HDA_GV.incrementSubrayLeafFailedTminCheckCount();
                }
                else if(!tmaxCheck){
                    HDA_GV.incrementSubrayLeafFailedTmaxCheckCount();
                }

            }
            if(
                nonNegativeDistance && closerThanClosest &&
                tminCheck && tmaxCheck
#else
            if(
			    newDistance > 0 && newDistance < search.closestHitDistance &&
			    newDistance >= tmin && newDistance <= tmax
#endif
            ){
				if(
				    !search.groundOnly ||
				    (prim->material != nullptr && prim->material->isGround)
                ){
					search.closestHitDistance = newDistance;
					hitPrim = prim;
				}
			}
		}
	}
	// ######### END If node is a leaf, perform ray-primitive intersection on all primitives in the leaf's bucket ###########

	// ######### BEGIN If node is not a leaf, figure out which child node(s) to traverse next, in which order #############
	else {

		int const a = node->splitAxis;

		double thit = numeric_limits<double>::infinity();

		LightKDTreeNode* first = nullptr;
		LightKDTreeNode* second = nullptr;

		// ############ BEGIN Check ray direction to figure out thorugh which sides the ray passes in which order ###########

		// Case 1: Ray goes in positive direction - it passes through the left side first, then through the right:
		if (search.rayDirArray[a] > 0) {
#ifdef DATA_ANALYTICS
            ++positiveDirectionCount;
#endif
			first = node->left;
			second = node->right;

			thit = (node->splitPos - search.rayOriginArray[a]) /
			    search.rayDirArray[a];
		}
		// Case 2: Ray goes in negative direction - it passes through the right side first, then through the left:
		else if (search.rayDirArray[a] < 0) {
#ifdef DATA_ANALYTICS
            ++negativeDirectionCount;
#endif
			first = node->right;
			second = node->left;

			thit = (node->splitPos - search.rayOriginArray[a]) /
			    search.rayDirArray[a];
		}
		// Case 3: Ray goes parallel to the split plane - it passes through only one side, depending on it's originWaypoint:
		else {
#ifdef DATA_ANALYTICS
            ++parallelDirectionCount;
#endif
			first = (search.rayOriginArray[a] < node->splitPos) ?
			    node->left : node->right;
			second = (search.rayOriginArray[a] < node->splitPos) ?
			    node->right : node->left;
		}
		// ############ END Check ray direction to figure out thorugh which sides the ray passes in which order ###########

		// ########### BEGIN Check where the ray crosses the split plane to figure out which sides we need to stop into at all ###########

		// thit >= tmax means that the ray crosses the split plane *after it has already left the volume*.
		// In this case, it never enters the second half.
		if (thit >= tmax) {
#ifdef DATA_ANALYTICS
            ++noSecondHalfCount;
#endif
			hitPrim = search_recursive(
                first, tmin, tmax, search
#ifdef DATA_ANALYTICS
                ,positiveDirectionCount,
                negativeDirectionCount,
                parallelDirectionCount,
                noSecondHalfCount,
                noFirstHalfCount,
                bothSidesCount,
                bothSidesSecondTryCount
#endif
            );
		}

		// thit <= tmin means that the ray crosses the split plane *before it enters the volume*.
		// In this case, it never enters the first half:
		else if (thit <= tmin) {
#ifdef DATA_ANALYTICS
            ++noFirstHalfCount;
#endif
			hitPrim = search_recursive(
                second, tmin, tmax, search
#ifdef DATA_ANALYTICS
               ,positiveDirectionCount,
               negativeDirectionCount,
               parallelDirectionCount,
               noSecondHalfCount,
               noFirstHalfCount,
               bothSidesCount,
               bothSidesSecondTryCount
#endif
            );
		}

		// Otherwise, the ray crosses the split plane within the volume.
		// This means that it passes through both sides:
		else {
#ifdef DATA_ANALYTICS
            ++bothSidesCount;
#endif
			hitPrim = search_recursive(
                first, tmin, thit+epsilon, search
#ifdef DATA_ANALYTICS
                ,positiveDirectionCount,
                negativeDirectionCount,
                parallelDirectionCount,
                noSecondHalfCount,
                noFirstHalfCount,
                bothSidesCount,
                bothSidesSecondTryCount
#endif
            );

			if (hitPrim == nullptr) {
				hitPrim = search_recursive(
                    second, thit-epsilon, tmax, search
#ifdef DATA_ANALYTICS
                    ,positiveDirectionCount,
                    negativeDirectionCount,
                    parallelDirectionCount,
                    noSecondHalfCount,
                    noFirstHalfCount,
                    bothSidesCount,
                    bothSidesSecondTryCount
#endif
                );
			}
		}

		// ########### END Check where the ray crosses the split plane to figure out which sides we need to stop into at all ###########
	}
	// ######### END If node is not a leaf, figure out which child node(s) to traverse next, in which order #############

	return hitPrim;
}
