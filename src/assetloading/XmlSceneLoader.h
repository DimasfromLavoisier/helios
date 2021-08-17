#pragma once

#include <tinyxml2.h>

#include <Scene.h>
#include <SceneLoadingSpecification.h>
#include <scene/dynamic/DynSequentialMovingObject.h>

/**
 * @brief Class for scene loading from XML file.
 *
 * Whenever possible, it is preferred to load XML defined components by
 *  XmlAssetsLoader or XmlSurveyLoader classes which are the main classes for
 *  this purpose.
 *
 * @see XmlAssetsLoader
 */
class XmlSceneLoader {
public:
    // ***  ATTRIBUTES  *** //
    // ******************** //
    /**
	 * @brief Scene loading specification
	 * @see SceneLoadingSpecification
	 */
    SceneLoadingSpecification sceneSpec;

    // ***  CONSTRUCTION / DESTRUCTION  *** //
    // ************************************ //
    XmlSceneLoader() = default;
    virtual ~XmlSceneLoader() {}

    // ***  SCENE CREATION  *** //
    // ************************ //
    /**
	 * @brief Create scene from given XML element (node)
	 * @param sceneNode XML element (node) containing scene data
	 * @param path Path to scene file
	 * @return Shared pointer to created scene
	 * @see Scene
	 */
    std::shared_ptr<Scene> createSceneFromXml(
        tinyxml2::XMLElement* sceneNode,
        std::string path
    );

    /**
     * @brief Load filters defining the scene part.
     *
     * NOTICE a scene part requires at least one primitives loading filter to
     *  be instantiated, otherwise it will be nullptr
     *
     * @param scenePartNode XML part node defining the scene part
     * @param[out] holistic Used to specify if all vertices defining each
     *  primitive must be considered as a whole (true) or not
     * @return Built scene part if any, nullptr otherwise
     */
    ScenePart * loadFilters(
        tinyxml2::XMLElement *scenePartNode,
        bool &holistic
    );

    // TODO Rethink : Document
    shared_ptr<DynSequentialMovingObject> loadRigidMotions(
        tinyxml2::XMLElement *scenePartNode,
        ScenePart *scenePart
    );

    /**
     * @brief Load the scene part identifier
     * @param scenePartNode XML part node where the identifier might be
     *  explicitly specified
     * @param partIndex Index of scene part according to current loop
     *  iteration. It will be used if no specific identifier is provided
     *  through XML
     * @param scenePart The scene part object to which identifier must be
     *  assigned
     * @return True if scene part must be splitted, false otherwise. A scene
     *  part can only be splitted when a part identifier is explicitly provided
     */
    bool loadScenePartId(
        tinyxml2::XMLElement *scenePartNode,
        int partIndex,
        ScenePart *scenePart
    );

    /**
     * @brief Apply final processings to the built scene part so it is fully
     *  integrated in the scene and totally configured
     * @param scenePart The scene part object to be digested
     * @param scene The scene where the scene part belongs
     * @param holistic Flag used to specify if all vertices defining each
     *  primitive must be considered as a whole (true) or not (false)
     * @param splitPart Flag to specify if scene part must be splitted into
     *  subparts (true) or not (false)
     * @param[out] partIndex If the subpart is splitted, then partIndex will
     *  be opportunely updated
     * @see ScenePart::splitSubparts
     */
    void digestScenePart(
        ScenePart *scenePart,
        std::shared_ptr<Scene> scene,
        bool holistic,
        bool splitPart,
        int &partIndex
    );
};