#pic_to_monochrome_V1.py
(Optional) script to pre-process a camnera image/phtos, especially those of high bit sizes to that of only 4-bit, grey scale.

Change only the input and output file name to process in the working folder of this script: 
<img width="1038" height="515" alt="image" src="https://github.com/user-attachments/assets/268d0b0e-eb89-4146-a408-07852914415d" />

#Image_bin_generator_V2.py
This script does 2 actions in 1 run:

1) Initial part of the script takes the input jpg, and convert to true 2 bit monochrome with a default threshold set as 128 (can be changed). This image is saved as an intermediate binary file and also as a preview jpg for quick reference before actual display on monochrome RLCD

2) Second part converts the binary file to that of .h file, so that the main code can now use this generated file directly for the RLCD 

Chenge the input .jpg filename for processing, and then the output h file expected  
<img width="846" height="92" alt="image" src="https://github.com/user-attachments/assets/4b77061a-5843-42d4-9e30-d298f3175c8e" />

<img width="602" height="262" alt="image" src="https://github.com/user-attachments/assets/3ba4bf1c-59f5-4d37-99cc-a81658651988" />


