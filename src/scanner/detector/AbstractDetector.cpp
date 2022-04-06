#include <scanner/detector/AbstractDetector.h>
#include <filems/facade/FMSFacade.h>
#include <logging.hpp>

// ***  CONSTRUCTION / DESTRUCTION  *** //
// ************************************ //
void AbstractDetector::_clone(std::shared_ptr<AbstractDetector> ad){
    ad->scanner = scanner; // Reference pointer => copy pointer, not object
    ad->cfg_device_accuracy_m = cfg_device_accuracy_m;
    ad->cfg_device_rangeMin_m = cfg_device_rangeMin_m;
    ad->fms = fms;
}


// ***  M E T H O D S  *** //
// *********************** //
void AbstractDetector::shutdown() {
    fms->write.finishMeasurementWriter();
}
void AbstractDetector::onLegComplete(){
    pcloudYielder->yield();
}

// ***  GETTERs and SETTERs  *** //
// ***************************** //
void AbstractDetector::setFMS(std::shared_ptr<FMSFacade> fms) {
    this->fms = fms;
    if(fms != nullptr){
        pcloudYielder = std::make_shared<PointcloudYielder>(fms->write);
    }
}
