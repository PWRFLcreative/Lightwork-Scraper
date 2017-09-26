#include "ofApp.h"

//--------------------------------------------------------------
void ofApp::setup(){
ofSetLogLevel(OF_LOG_NOTICE); //Set the log level

camWidth = 640;
camHeight = 480;
camAspect = (float)camWidth / (float)camHeight;

//Check for hi resolution display
int guiMultiply = 1;
if (ofGetScreenWidth() >= RETINA_MIN_WIDTH) {
	guiMultiply = 2;
}

//Window size based on screen dimensions, centered
ofSetWindowShape((int)ofGetScreenHeight() / 2 * camAspect + (200 * guiMultiply), (int)ofGetScreenHeight()*0.9);
ofSetWindowPosition((ofGetScreenWidth() / 2) - ofGetWindowWidth() / 2, ((int)ofGetScreenHeight() / 2) - ofGetWindowHeight() / 2);

//Fbos
camFbo.allocate(camWidth, camHeight);
camFbo.begin();
ofClear(255, 255, 255);
camFbo.end();

ofLogToConsole();

int framerate = 20; // Used to set oF and camera framerate
ofSetFrameRate(framerate);
ofBackground(ofColor::black);
ofSetWindowTitle("LightWork");

//Video Devices
devices = cams[0].listDevices(); 
for (int i=0; i < devices.size(); i++) {
	cams[i].setVerbose(false);
	cams[i].setDeviceID(i); // Default to external camera
	cams[i].setPixelFormat(OF_PIXELS_RGB);
	cams[i].setup(camWidth, camHeight);
}
camPtr = &cams[0];

learningTime.set("Learning Time", 4, 0, 30);
thresholdValue.set("Threshold Value", 50, 0, 255);

// Contours
contourFinder.setMinAreaRadius(1);
contourFinder.setMaxAreaRadius(100);
contourFinder.setThreshold(15);
// wait for half a frame before forgetting something (15)
contourFinder.getTracker().setPersistence(1);
// an object can move up to 32 pixels per frame
contourFinder.getTracker().setMaximumDistance(32);
contourFinder.getTracker().setSmoothingRate(1.0);

// Allocate the thresholded view so that it draws on launch (before calibration starts).
thresholded.allocate(camWidth, camHeight, OF_IMAGE_COLOR);
thresholded.clear();

// LED
IP = "192.168.1.104"; //Default IP for Fadecandy

// Handle 'skipped' LEDs. This covers LEDs that are not visible (and shouldn't be, because reasons... something something hardware... hacky... somthing...)
hasFoundFirstContour = false;

// Animator settings
animator.setMode(ANIMATION_MODE_CHASE);
animator.setNumLedsPerStrip(50);
animator.setAllLEDColours(ofColor(0, 0, 0));

// Tracking
hasFoundFirstContour = false;
isMapping = false;

// Connect to the fcserver
opcClient.setup(IP, 7890, 1, animator.getNumLedsPerStrip());
opcClient.setInterpolation(false);

// Clear the LED strips
animator.setAllLEDColours(ofColor(0, 0, 0));
opcClient.autoWriteData(animator.getPixels());

// SVG
svg.setViewbox(0, 0, camWidth, camHeight);

//GUI
buildUI(guiMultiply);
}

//--------------------------------------------------------------
void ofApp::update() {
	opcClient.update();

	//// If the client is not connected do not try to send information
	//if (!opcClient.isConnected()) {
	//	// Will continue to try connecting to the OPC Pixel Server
	//	opcClient.tryConnecting();
	//}

	if (animator.mode == ANIMATION_MODE_TEST) {
		animator.update(); // Update the pixel values
		opcClient.autoWriteData(animator.getPixels()); // Send pixel values to OPC
	}

	camPtr->update();

    // Background subtraction
    background.setLearningTime(learningTime);
    background.setThresholdValue(thresholdValue);
    background.update(*camPtr, thresholded);
    thresholded.update();
    
    // New camera frame: Turn on a new LED and detect the location.
    // We are getting every third camera frame (to give the LEDs time to light up and the camera to pick it up).
    if(camPtr->isFrameNew() && (animator.mode == ANIMATION_MODE_CHASE) && isMapping && (ofGetFrameNum()%3 == 0)) {
        bool success = false; // Indicate if we successfully mapped an LED on this frame (visible or off-canvas)
        
        // Light up a new LED for every frame
        animator.update();
        opcClient.autoWriteData(animator.getPixels()); // TODO: review write calls (see below)
        
        // Contour
        ofxCv::blur(thresholded, 10);
        contourFinder.findContours(thresholded);
        
        // We have 1 contour
        if (contourFinder.size() == 1 && !success) { // TODO: review isLedOn vs isMapping()
            ofLogVerbose("tracking") << "Detected one contour, as expected.";
            ofPoint center = ofxCv::toOf(contourFinder.getCenter(0));
            centroids.push_back(center);
            success = true;
        }
        
        // We have more than 1 contour, select the brightest one.
        else if (contourFinder.size() > 1 && !success){ // TODO: review isLedOn vs isMapping()
            ofLogVerbose("tracking") << "num contours: " << ofToString(contourFinder.size());
            int brightestIndex = 0;
            int previousBrightness = 0;
            for(int i = 0; i < contourFinder.size(); i++) {
                int brightness = 0;
                cv::Rect rect = contourFinder.getBoundingRect(i);
                //ofLogNotice("x:" + ofToString(rect.x)+" y:"+ ofToString(rect.y)+" w:" + ofToString(rect.width) + " h:"+ ofToString(rect.height));
                ofImage img;
                img = thresholded;
                img.crop(rect.x, rect.y, rect.width, rect.height);
                ofPixels pixels = img.getPixels();
                
                for (int i = 0; i< pixels.size(); i++) {
                    brightness += pixels[i];
                }
                brightness /= pixels.size();
                
                // Check if the brightness is greater than the previous contour brightness
                if (brightness > previousBrightness) {
                    brightestIndex = i;
                }
                previousBrightness = brightness;
                success = true;
                //ofLogNotice("Brightness: " + ofToString(brightness));
            }
            ofLogNotice("tracking") << "brightest index: " << ofToString(brightestIndex);
            ofPoint center = ofxCv::toOf(contourFinder.getCenter(brightestIndex));
            centroids.push_back(center);
            hasFoundFirstContour = true;
            //ofLogVerbose("tracking") << "added point, ignored additional points. FrameCount: " << ofToString(ofGetFrameNum())+ " ledIndex: " << animator.ledIndex+(animator.currentStripNum-1)*animator.numLedsPerStrip;
        }
        
        // Deal with no contours found
        else if (!success && hasFoundFirstContour){
            ofLogVerbose("tracking") << "NO CONTOUR FOUND!!!";
            
            // No point detected, create fake point
            ofPoint fakePoint;
            fakePoint.set(0, 0);
            centroids.push_back(fakePoint);
            success = true;
            //ofLogVerbose("tracking") << "CREATING FAKE POINT                     at frame: " << ofToString(ofGetFrameNum()) << " ledIndex " << animator.ledIndex+(animator.currentStripNum-1)*animator.numLedsPerStrip;
        }
        
        if(success) {
            hasFoundFirstContour = true;
            //animator.chaseAnimationOff();
            opcClient.autoWriteData(animator.getPixels());
        }
    }
    ofSetColor(ofColor::white);
}

//--------------------------------------------------------------
void ofApp::draw(){
	//Draw into Fbo to allow scaling regardless of camera resolution
	camFbo.begin();
	camPtr->draw(0,0);

    ofxCv::RectTracker& tracker = contourFinder.getTracker();
    
    ofSetColor(0, 255, 0);
	contourFinder.draw(); // Draws the blob rect surrounding the contour
	
    // Draw the detected contour center points
    for (int i = 0; i < centroids.size(); i++) {
		ofDrawCircle(centroids[i].x, centroids[i].y, 3);
    }
	camFbo.end();

	ofSetColor(ofColor::white); //reset color, else it tints the camera

	//Draw Fbo and Thresholding images to screen
	camFbo.draw(0, 0, (ofGetWindowHeight() / 2)*camAspect, ofGetWindowHeight()/2);
	if (thresholded.isAllocated()) {
		thresholded.draw(0, ofGetWindowHeight() / 2, (ofGetWindowHeight() / 2)*camAspect, ofGetWindowHeight()/2);
	}

}

//--------------------------------------------------------------
void ofApp::keyPressed(int key){
    switch (key){
        case ' ':
            centroids.clear();
            break;
        case 's':
            centroids.clear();
            isMapping = !isMapping;
            animator.setMode(ANIMATION_MODE_CHASE);
            opcClient.autoWriteData(animator.getPixels());
            break;
        case 'g':
            generateSVG(centroids);
            break;
        case 'j':
            generateJSON(centroids);
            break;
		case 't':
            animator.setMode(ANIMATION_MODE_TEST);
            opcClient.autoWriteData(animator.getPixels());
			break;
        case 'f': // filter points
            centroids = removeDuplicatesFromPoints(centroids);
    }

}

//--------------------------------------------------------------
void ofApp::keyReleased(int key){

}

//--------------------------------------------------------------
void ofApp::mouseMoved(int x, int y ){

}

//--------------------------------------------------------------
void ofApp::mouseDragged(int x, int y, int button){

}

//--------------------------------------------------------------
void ofApp::mousePressed(int x, int y, int button){

}

//--------------------------------------------------------------
void ofApp::mouseReleased(int x, int y, int button){
    
}

//--------------------------------------------------------------
void ofApp::mouseEntered(int x, int y){

}

//--------------------------------------------------------------
void ofApp::mouseExited(int x, int y){

}

//--------------------------------------------------------------
void ofApp::windowResized(int w, int h){
	ofBackground(0, 0, 0);
	/*buildUI();*/

}

//--------------------------------------------------------------
void ofApp::gotMessage(ofMessage msg){

}

//--------------------------------------------------------------
void ofApp::dragEvent(ofDragInfo dragInfo){ 

}

void ofApp::exit(){
	// Close Connection
	opcClient.close();
}

void ofApp::generateSVG(vector <ofPoint> points) {
	if (points.size() == 0) {
		//User is trying to save without anything to output - bail
		ofLogError("No point data to save, run mapping first");
		return;
	}
	
	ofPath path;
    for (int i = 0; i < points.size(); i++) {
        // Avoid generating a moveTo AND lineTo for the first point
        // If we don't specify the first moveTo message then the first lineTo will also produce a moveTo point with the same coordinates
        if (i == 0) {
            path.moveTo(points[i]);
        }
        else {
           path.lineTo(points[i]);
        }
        ofLogVerbose("output") << points[i].x << ", "<< points[i].y;
    }
    svg.addPath(path);
    path.draw();
	if (centroids.size() == 0) {
		//User is trying to save without anything to output - bail
		ofLogError("No point data to save, run mapping first");
		return;
	}

	ofFileDialogResult saveFileResult = ofSystemSaveDialog("layout.svg", "Save layout file");
	if (saveFileResult.bSuccess) {
		svg.save(saveFileResult.filePath);
        ofLogNotice("output") << "Saved SVG file.";
	}
}

void ofApp::generateJSON(vector<ofPoint> points) {
    int maxX = ofToInt(svg.info.width);
    int maxY = ofToInt(svg.info.height);
    ofLogNotice("output") << "maxX, maxY: " << maxX << ", " << maxY;
	//ofLogNotice() << maxX;
	//ofLogNotice() << maxY;
    
    ofxJSONElement json; // For output
    
    for (int i = 0; i < points.size(); i++) {
        Json::Value event;
        Json::Value vec(Json::arrayValue);
        vec.append(Json::Value(points[i].x/maxX)); // Normalized
        vec.append(Json::Value(0.0)); // Normalized
        vec.append(Json::Value(points[i].y/maxY));
        event["point"]=vec;
        json.append(event);
    }
    
    json.save("testLayout.json");
    ofLogNotice("output") << "Saved JSON file.";
}


/*
 I'm expecting a series of 2D points. I need to filter out points that are too close together, but keep
 negative points. The one that are negative represent 'invisible' or 'skipped' LEDs that have a physical presence
 in an LED strip but are not visuable. We need to store them 'off the canvas' so that our client application (Lightwork Scraper) can be aware of the missing LEDs (as they are treated sequentially, with no 'fixed' address mapping.
 IDEA: Can we store the physical address as the 'z' in a Vec3 or otherwise encode it in the SVG. Maybe we can make another
 'path' in the SVG that stores the address in a path of the same length.
 */
vector <ofPoint> ofApp::removeDuplicatesFromPoints(vector <ofPoint> points) {
    ofLogNotice("tracking") << "Removing duplicates";
    
    float thresh = 3.0; // TODO: Add interface to GUI
    
    std::vector<ofPoint>::iterator iter;
    
    // Iterate through all the points and remove duplicates and 'extra' points (under threshold distance).
    for (iter = points.begin(); iter < points.end(); iter++) {
        int i = std::distance(points.begin(), iter); // Index of iter, used to avoid comporating a point to itself
        ofPoint pt = *iter;
        ofLogVerbose("tracking") << "BASE: " << pt << endl;
        
        // Do not remove 0,0 points (they're 'invisible' LEDs, we need to keep them).
        if (pt.x == 0 && pt.y == 0) {
            continue; // Go to the next iteration
        }
        
        // Compare point to all other points
        std::vector<ofPoint>::iterator j_iter;
        for (j_iter = points.begin(); j_iter < points.end(); j_iter++) {
            int j = std::distance(points.begin(), j_iter); // Index of j_iter
            ofPoint pt2 = *j_iter;
            ofLogVerbose("tracking") << "NESTED: " << pt2 << endl;
            float dist = pt.distance(pt2);
            ofLogVerbose("tracking") << "DISTANCE: " << dist << endl;
            ofLogVerbose("tracking") << "i: " << i << " j: " << j << endl;
            // Comparing point to itself... do nothing and move on.
            if (i == j) {
                ofLogVerbose("tracking") << "COMPARING POINT TO ITSELF " << pt << endl;
                continue; // Move on to the next j point
            }
            // Duplicate point detection. (This might be covered by the distance check below and therefor redundant...)
            else if (pt.x == pt2.x && pt.y == pt2.y) {
                ofLogVerbose("tracking") << "FOUND DUPLICATE POINT (that is not 0,0) - removing..." << endl;
                iter = points.erase(iter);
                break;
            }
            // Check point distance, remove points that are too close
            else if (dist < thresh) {
                ofLogVerbose("tracking") << "REMOVING" << endl;
                iter = points.erase(iter);
                break;
            }
        }
    }
    
    return points;
}

//Dropdown Handler
void ofApp::onDropdownEvent(ofxDatGuiDropdownEvent e)
{
	if (e.target->is("Select Camera")) {
		vector<string> devices = enumerateCams();
		switchCamera(e.child, camWidth, camHeight);
		//TODO - figure out how to repopulate the list
		//ofLogNotice() << "Camera " << e.child << " was selected";
		guiBottom->getLabel("Message Area")->setLabel((gui->getDropdown("Select Camera")->getChildAt(e.child)->getLabel())+" selected");
		int guiMultiply = 1;
		if (ofGetScreenWidth() >= RETINA_MIN_WIDTH) {
			guiMultiply = 2;
		}
		ofSetWindowShape((int)ofGetScreenHeight() / 2 * camAspect + (200 * guiMultiply), (int)ofGetScreenHeight()*0.9);
		gui->update();
	}

	if (e.target->is("Select Driver Type")) {
		if (e.child == 0) {
			ofLogNotice() << "Pixel Pusher was selected";
			guiBottom->getLabel("Message Area")->setLabel("Pixel Pusher selected");
			gui->getFolder("PixelPusher Settings")->setVisible(true);
			gui->getFolder("PixelPusher Settings")->expand();
			gui->getFolder("Mapping Settings")->setVisible(true);
			gui->getFolder("Mapping Settings")->expand();
			gui->getFolder("Fadecandy Settings")->setVisible(false);
			gui->getFolder("Fadecandy Settings")->collapse();
		}
		else if (e.child == 1) {
			ofLogNotice() << "Fadecandy/Octo was selected";
			guiBottom->getLabel("Message Area")->setLabel("Fadecandy/Octo selected");
			gui->getFolder("Fadecandy Settings")->setVisible(true);
			gui->getFolder("Fadecandy Settings")->expand();
			gui->getFolder("Mapping Settings")->setVisible(true);
			gui->getFolder("Mapping Settings")->expand();
			gui->getFolder("PixelPusher Settings")->setVisible(false);
			gui->getFolder("PixelPusher Settings")->collapse();
		}
	}
}

//GUI event handlers
void ofApp::onSliderEvent(ofxDatGuiSliderEvent e)
{
		ofLogNotice() << "onSliderEvent: " << e.target->getLabel() << " "; e.target->printValue(); //TODO: stop from spamming output
		//if (e.target->is("gui opacity")) gui->setOpacity(e.scale);
}

void ofApp::onTextInputEvent(ofxDatGuiTextInputEvent e)
{
	ofLogNotice() << "onTextInputEvent: " << e.target->getLabel() << " " << e.target->getText();

	if (e.target->is("IP")) {
		IP= e.target->getText();
		if (opcClient.isConnected()) {
			opcClient.close();
			if (!opcClient.isConnected()) { gui->getLabel("Connection Status")->setLabel("Disconnected"); }
		}
		if (!opcClient.isConnected()) {
			opcClient.setup(IP, 7890);
			if (opcClient.isConnected()) { gui->getLabel("Connection Status")->setLabel("Connected"); }
		}
	}

	if (e.target->is("LEDS per Strip")) {
		string temp = e.target->getText();
        animator.setNumLedsPerStrip(ofToInt(temp));
	}

	if (e.target->is("STRIPS")) {
		string temp = e.target->getText();
		animator.setNumStrips(ofToInt(temp));
	}
}

void ofApp::onButtonEvent(ofxDatGuiButtonEvent e)
{
	ofLogNotice() << "onButtonEvent: " << e.target->getLabel();

	if (e.target->is("TEST LEDS")) {
        animator.setMode(ANIMATION_MODE_TEST);
        opcClient.autoWriteData(animator.getPixels());
	}
	if (e.target->is("MAP LEDS")) {
        isMapping = !isMapping;
	}
	if (e.target->is("SAVE LAYOUT")) {
		centroids = removeDuplicatesFromPoints(centroids);
		generateSVG(centroids);
	}

}

//Used to change acctive camera during runtime. Necessary to close old camera first before initializing the new one.
void ofApp::switchCamera(int num, int w, int h)
{
	//ofLogNotice("Switching camera");
	//if(cam.isInitialized()){
	//	cam.close();
	//	//ofLogNotice() << cam.isInitialized();
	//	cam2.setDeviceID(num);
	//	cam2.setPixelFormat(OF_PIXELS_RGB);
	//	cam2.setup(w, h);
	//	camPtr = &cam2;
	//}	
	//
	//else if (cam2.isInitialized()) {
	//	cam2.close();
	//	cam.setDeviceID(num);
	//	cam.setPixelFormat(OF_PIXELS_RGB);
	//	cam.setup(w, h);
	//	camPtr = &cam;
	//}

	camPtr = &cams[num];

}
//Returns a vector containing all the attached cameras
vector<string> ofApp::enumerateCams()
{
	devices.clear();
	devices = cams[0].listDevices();
	vector<string> deviceStrings;

	for (std::vector<ofVideoDevice>::iterator it = devices.begin(); it != devices.end(); ++it) {
		int i = std::distance(devices.begin(), it);
		ofVideoDevice device = *it;
		string name = device.deviceName;
		int id = device.id;
		//ofLogNotice() << "Camera " << id << ": " <<  name << endl;
		deviceStrings.push_back(name);

	}
		return deviceStrings;
}

void ofApp::buildUI(int mult)
{
	//GUI
	gui = new ofxDatGui(ofxDatGuiAnchor::TOP_RIGHT);
	guiBottom = new ofxDatGui(ofxDatGuiAnchor::BOTTOM_RIGHT);
	//gui->setTheme(new ofxDatGuiThemeSmoke());
	//gui->addHeader(":: drag me to reposition ::");

	gui->addDropdown("Select Camera", enumerateCams());
	gui->addBreak();

	vector<string> opts = { "PixelPusher", "Fadecandy/Octo" };
	gui->addDropdown("Select Driver Type", opts);
	gui->addBreak();

	ofxDatGuiFolder* fcSettings = gui->addFolder("Fadecandy Settings", ofColor::white);
	fcSettings->addTextInput("IP", IP);

	fcSettings->addTextInput("STRIPS", ofToString(animator.getNumStrips()));
	fcSettings->addTextInput("LEDS per Strip", ofToString(animator.getNumLedsPerStrip()));
	string connection;
	if (opcClient.isConnected()) {
		connection = "connected";
		fcSettings->addLabel("Connection Status");
		gui->getLabel("Connection Status")->setLabel(connection);
	}
	else {
		connection = "disconnected";
		fcSettings->addLabel("Connection Status");
		gui->getLabel("Connection Status")->setLabel(connection);
	}
	fcSettings->setVisible(false);
	fcSettings->addBreak();
	
	ofxDatGuiFolder* ppSettings = gui->addFolder("PixelPusher Settings", ofColor::white);
	ppSettings->addTextInput("IP", IP);
	ppSettings->addTextInput("STRIPS", ofToString(animator.getNumStrips()));
	ppSettings->addTextInput("LEDS per Strip", ofToString(animator.getNumLedsPerStrip()));
	ppSettings->setVisible(false);
	ppSettings->addBreak();


	

	ofxDatGuiFolder* mapSettings = gui->addFolder("Mapping Settings", ofColor::dimGrey);
	mapSettings->addSlider(learningTime);
	mapSettings->addSlider(thresholdValue);
	mapSettings->addButton("Test LEDS");
	mapSettings->addButton("Map LEDS");
	//gui->addButton(resetBackground);
	mapSettings->addButton("Save Layout");
	mapSettings->setVisible(false);
	mapSettings->addBreak();

	//Program Status GUI
	//guiBottom->addSlider("gui opacity", 0, 100, 50);
	guiBottom->addLabel("Message Area");
	guiBottom->addFRM()->setAnchor(ofxDatGuiAnchor::BOTTOM_RIGHT);
	//guiBottom->onSliderEvent(this, &ofApp::onSliderEvent);
	//guiBottom->

	//gui->addFooter();
	//gui->setOpacity(gui->getSlider("gui opacity")->getScale());

	// once the gui has been assembled, register callbacks to listen for component specific events //
	gui->onButtonEvent(this, &ofApp::onButtonEvent);
	//gui->onToggleEvent(this, &ofApp::onToggleEvent);
	gui->onSliderEvent(this, &ofApp::onSliderEvent);
	gui->onTextInputEvent(this, &ofApp::onTextInputEvent);
	//gui->on2dPadEvent(this, &ofApp::on2dPadEvent);
	gui->onDropdownEvent(this, &ofApp::onDropdownEvent);
	//gui->onColorPickerEvent(this, &ofApp::onColorPickerEvent);
	//gui->onMatrixEvent(this, &ofApp::onMatrixEvent);
}
