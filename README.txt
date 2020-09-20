This project incluse some ai and machine learing programs. A partial of the is my homework in JAIST.

Prepare for building (comm):
    - The auto configuration tool is base on Windows PowerShell script.
	  If this is your first time using PowerShell, import a registry file
      ".\tool\powershell-enable.reg" to enable PowerShell script.	

(for vs2008)	  
(1) Start up Windows PowerShell
(2) Enter direct "<workspace>\tool"
	- We suppose the <workspace> dir is trunk. If you download all reporsitory as your workspace, please uset "<workspace>\trunk\tool"
(3) Run ".\auto_config.ps1"
	.\auto_config.ps1 [-path_vld <VLD_PATH>] -path_boost <BOOST_PATH> -path_opencv <OPENCV_PATH>
	

(for vs2017)	  
(1) Start up Windows PowerShell

(2) Enter direct "<workspace>\tool"
	- We suppose the <workspace> dir is trunk. If you download all reporsitory as your workspace, please uset "<workspace>\trunk\tool"

(3) Run ".\auto_config_2.ps1"
	.\auto_config_2.ps1 [-path_vld <VLD_PATH>] -path_boost <BOOST_PATH> -path_opencv <OPENCV_PATH>
	It creates a .\build folder under workspace. All intermediate files and target files are created under this folder.
	
(4) Run ".\auto_build.ps1" to build common libraries. 
	.\auto_build.ps1
	We suppose your vs2017 has been installed in "C:\Program Files\Microsoft Visual Studio\2017\Community" as default.
	If not, please edit $vs_dir in .\auto_build.ps1 to set your vs2017 folder. 
	

Necessary libraries
		necessary ver.		tested ver.
vld		1.5 included		1.5 / 2.1
boost						1.46.1 / 1.59.0
opencv						2.3.1					
 
				
 