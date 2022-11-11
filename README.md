## Tech Stack
* Windows 11 Pro, 64-bit operating system, x64-based processor
* Visual Studio 2022
* Android Studio Chipmunk | 2021.2.1

## Getting started
### PC
1. Use Visual Studio Installer to get all individual components related to C++ (not just workloads)
2. Download the Vulkan SDK installer program for version 1.3.224.1 at https://vulkan.lunarg.com/sdk/home
3. Run the installer program and then download every tool, package, etc... (the installer should automatically set/update the VULKAN_SDK, VK_SDK_PATH, and PATH environment variables)
4. Allow **Unkown Sources** in *Settings > General* in the Oculus PC application
5. Allow **Developer Runtime Features** in *Settings > Beta* in the Oculus PC application
6. Allow **Passthrough over Oculus Link** in *Settings > Beta* in the Oculus PC application
7. Restart your computer
### Quest 2
8. Create an organization at https://developer.oculus.com/manage/organizations/create/
9. Verify your account at https://developer.oculus.com/manage/verify/
10. Enable developer-mode on your Quest2 by turning on the **USB Connection Dialog** option at *Settings > System > Developer*
11. Download and setup Meta Quest Developer Hub https://developer.oculus.com/downloads/native-android/
12. Give permission to access data and allow USB debugging after connecting your headset to your computer using a USB-C cable and putting on the headset
13. The Oculus Developer Hub should install the Oculus ADB Drivers but you can also install them with dependencies\oculus-adb-driver-2.0\oculus-go-adb-driver-2.0\usb_driver\android_winusb.inf using Device Manager
14. Create a new temporary project in Android Studio and then use the SDK Manager to install the following:
    1. Android SDK 10.0 (API level 29)
    2. Android SDK Build Tools (version 33.0.0)
    3. Android SDK Command-line Tools (version 7.0)
    4. Android NDK (version 25.1.8937393)
    5. CMake (version 3.22.1)
15. Set the following environment variables:
    1. JAVA_HOME (JDK location typically at C:\Program Files\Android\Android Studio\jre)
    2. ANDROID_HOME (Android SDK location typically at C:\Users\[username]\AppData\Local\Android\Sdk)
    3. ANDROID_NDK_HOME (Android NDK location typically at C:\Users\[username]\AppData\Local\Android\Sdk\ndk\[versionnumber])
16. Add the following directories to your PATH:
    1. JDK tools (typically C:\Program Files\Android\Android Studio\jre\bin)
    2. Android SDK platform-tools (typically C:\Users\[username]\AppData\Local\Android\Sdk\platform-tools)
    3. Android SDK tools (typically C:\Users\<username>\AppData\Local\Android\Sdk\tools)
17. Close the temporary project and then open the root level build.gradle file in Android Studio
18. Create a new keystore
    1. In Android Studio, select *Build > Generate Signed Bundle / APK*
    2. Select **APK**
    4. Select **Create new ...**
    5. Enter the required information (do not use "+" for country code)
    6. Select **Next**
    7. Select the **release** build variant
19. Enter your keystore information into the auto-generated keystore.properties file located in the root directory
20. Select *File > Sync Project with Gradle Files*
21. Use the command ```adb shell setprop debug.oculus.loadandinjectpackagedvvl.com.tom.bomberman_xr.debug 1``` from an Oculus Developer Hub custom command or a command prompt (**this MUST be run every time the headset is reset**)

\* *Open the project in Visual Studio with the bomberman_xr.sln file and in Android Studio with the build.gradle file at the root level*

## Help
### Coordinate System and Units
* It's a right-handed coordinate system (+X right, +Y up, -Z forward)
* Positive rotation is counterclockwise about the axis of rotation
* The units for all linear distances are meters
* The texture coordinates (0, 0) correspond to the upper left corner of the image
![Alt text](help/coordinate_system.png?raw=true "Coordinate System")

### Change the width of the Solution Configuration drop-down list in Visual Studio 2022
1. Select *Tools > Customize...*
2. In the *Commands* tab, select *Toolbar:* then *Standard* in corresponding drop-down list
3. Select *Solution Configurations* and then *Modify Selection*

### Add a Vulkan glsl shader source file to Visual Studio 2022
1. Make sure the source file is located in shaders/glsl/
2. Right-click the "bomberman_xr" project and select *Add > Existing Item...* then select the source file
3. Right-click the source file in the Solution Explorer and select *Properties*
4. Select the debug configuration for your target platform(s)
    1. Set the *Item Type* property to **Custom Build Tool** then select *Apply*
    2. Select the *Custom Build Tool* property that just appeared under *General*
    3. Set the *Command Line* property to ```glslc.exe %(Identity) -mfmt=num -o "$(SolutionDir)shaders\glsl\bin\debug\%(Filename)%(Extension)".spv```
    4. Set the *Description* property to ```Compiling shader %(Identity) to $(SolutionDir)shaders\glsl\bin\debug\%(Filename)%(Extension).spv```
    5. Set the *Outputs* property to ```$(SolutionDir)shaders\glsl\bin\debug\%(Filename)%(Extension).spv```
    6. Set the *Link Objects* property to **No**
    7. Select *Apply*
5. Select the release configuration for your target platform(s)
    1. Set the *Command Line* property to ```glslc.exe %(Identity) -O -mfmt=num -o "$(SolutionDir)shaders\glsl\bin\release\%(Filename)%(Extension)".spv```
    2. Set the *Description* property to ```Compiling shader %(Identity) to $(SolutionDir)shaders\glsl\bin\release\%(Filename)%(Extension).spv```
    3. Set the *Outputs* property to ```$(SolutionDir)shaders\glsl\bin\release\%(Filename)%(Extension).spv```
    4. Set the *Link Objects* property to **No**
    5. Select *Apply*

\* *If loading SPIR-V code as a binary file (not as a list of comma-separated 32-bit hex integers) then remove ```-mfmt=num``` from the *Command Line* property*

### Miscellaneous
* Rest-pose, Bind-pose, and T-pose are all assumed to be equal
* All non-skinned meshes will be have their pivot automatically set to their center when loaded from a file
* All skinned meshes will be have thier pivot automatically set to their skeleton's root when loaded from a file
* A skinned mesh's bounding volume may need to be increased if animated
* Visual Studio must be run as administrator to use the Optick profiler
* You have to select *File > Sync Project with Gradle Files* if you change a build.gradle file (and possibly the CMakeLists.txt and AndroidManifest.xml files as well)
* I didn't need to run the following commands but if Vulkan validation layers are not working you can try running ```adb shell setprop debug.vvl.forcelayerlog 1``` and then ```adb logcat -s VALIDATION```
* Android Studio automatically signs your app with a debug certificate for debug builds. The debug certificate has an expiration date of 30 years from its creation date and you will have delete the debug.keystore file most likely stored at C:\Users\user\\.android\
* I get some unsettling logs while running the debug build. I am unsure if they are my fault and I have been ignoring them. You can view them [here](https://gist.github.com/THolovacko/9eeb46806942bf5a7fca5473e7215b60)
