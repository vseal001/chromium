'use strict';

MockDevice.prototype.setPose = function(pose) {
  if (pose == null) {
    this.pose_ = null;
  } else {
    this.pose_ = {
      orientation: null,
      position: null,
      angularVelocity: null,
      linearVelocity: null,
      angularAcceleration: null,
      linearAcceleration: null,
      inputState: null,
      poseReset: false,
      poseIndex: 0
    };
    for (var field in pose) {
      if (this.pose_.hasOwnProperty(field)) {
        this.pose_[field] = pose[field];
      }
    }
  }
};

MockDevice.prototype.forceActivate = function(reason) {
  this.displayClient_.onActivate(reason);
};

function vr_test(func, vrDisplays, name, properties) {
  let chain = Promise.resolve();
  let firstDeviceController;
  vrDisplays.forEach(display => {
    return chain.then(
        XRTest
            .simulateDeviceConnection(
                {supportsImmersive: display.capabilities.canPresent})
            .then((deviceController) => {
              deviceController.displayInfo_ = display;
              if (!firstDeviceController) {
                firstDeviceController = deviceController;
              }
            }));
  });
  chain.then(() => {
    let t = async_test(name, properties);
    func(t, firstDeviceController);
  })
}

function fakeVRDisplays(){
  let generic_left_fov = {
    upDegrees : 45,
    downDegrees : 45,
    leftDegrees : 50,
    rightDegrees : 40,
  };

  let generic_right_fov = {
    upDegrees : 45,
    downDegrees : 45,
    leftDegrees : 40,
    rightDegrees : 50,
  };

  let generic_left_eye = {
    fieldOfView : generic_left_fov,
    offset : [-0.03, 0, 0],
    renderWidth : 1024,
    renderHeight : 1024
  };

  let generic_right_eye = {
    fieldOfView :generic_right_fov,
    offset : [0.03, 0, 0],
    renderWidth : 1024,
    renderHeight : 1024
  };

  return {
    FakeMagicWindowOnly: {
      displayName : "FakeVRDisplay",
      capabilities : {
        hasPosition : false,
        hasExternalDisplay : false,
        canPresent : false
      },
      stageParameters : null,
      leftEye : null,
      rightEye : null,
      webvrDefaultFramebufferScale: 1.0,
    },

    FakeRoomScale: {
      displayName : "FakeVRDisplayRoom",
      capabilities : {
        hasPosition : true,
        hasExternalDisplay : true,
        canPresent : true,
        maxLayers : 1
      },
      stageParameters : {
        standingTransform : [0.0, 0.1, 0.2, 0.3,
                             0.4, 0.5, 0.6, 0.7,
                             0.8, 0.9, 1.0, 0.1,
                             0.2, 0.3, 0.4, 0.5],
        sizeX : 5.0,
        sizeZ : 3.0,
      },
      leftEye : generic_left_eye,
      rightEye : generic_right_eye,
      webvrDefaultFramebufferScale: 1.0,
    },

    Pixel: { // Pixel info as of Dec. 22 2016
      displayName : "Google, Inc. Daydream View",
      capabilities : {
        hasPosition : false,
        hasExternalDisplay : false,
        canPresent : true,
        maxLayers : 1
      },
      stageParameters : null,
      leftEye : {
        fieldOfView : {
          upDegrees : 48.316,
          downDegrees : 50.099,
          leftDegrees : 35.197,
          rightDegrees : 50.899,
        },
        offset : [-0.032, 0, 0],
        renderWidth : 1920,
        renderHeight : 2160
      },
      rightEye : {
        fieldOfView : {
          upDegrees : 48.316,
          downDegrees : 50.099,
          leftDegrees: 50.899,
          rightDegrees: 35.197
        },
        offset : [0.032, 0, 0],
        renderWidth : 1920,
        renderHeight : 2160
      },
      webvrDefaultFramebufferScale: 0.5,
    }
    // TODO(bsheedy) add more displays like Rift/Vive
  };
}
