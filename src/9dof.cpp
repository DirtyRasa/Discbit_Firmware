#include "application.h"
#include "9dof.h"
#include "quaternionFilters.h"
#include "MPU9250.h"
#include "discData.h"

MPU9250 myIMU;
unsigned long lastPublishTime = 0;

void setup9dof() {
  // Read the WHO_AM_I register, this is a good test of communication
  byte c = myIMU.readByte(MPU9250_ADDRESS, WHO_AM_I_MPU9250);
  Serial.print("MPU9250 "); Serial.print("I AM "); Serial.print(c, HEX);
  Serial.print(" I should be "); Serial.println(EXPECTED_MP9250_WHO_AM_I, HEX);

  if (c == EXPECTED_MP9250_WHO_AM_I) // WHO_AM_I should always be EXPECTED_MP9250_WHO_AM_I
  {
    Serial.println("MPU9250 is online...");

    // Start by performing self test and reporting values
    myIMU.MPU9250SelfTest(myIMU.SelfTest);
    Serial.print("x-axis self test: acceleration trim within : ");
    Serial.print(myIMU.SelfTest[0],1); Serial.println("% of factory value");
    Serial.print("y-axis self test: acceleration trim within : ");
    Serial.print(myIMU.SelfTest[1],1); Serial.println("% of factory value");
    Serial.print("z-axis self test: acceleration trim within : ");
    Serial.print(myIMU.SelfTest[2],1); Serial.println("% of factory value");
    Serial.print("x-axis self test: gyration trim within : ");
    Serial.print(myIMU.SelfTest[3],1); Serial.println("% of factory value");
    Serial.print("y-axis self test: gyration trim within : ");
    Serial.print(myIMU.SelfTest[4],1); Serial.println("% of factory value");
    Serial.print("z-axis self test: gyration trim within : ");
    Serial.print(myIMU.SelfTest[5],1); Serial.println("% of factory value");

    // Calibrate gyro and accelerometers, load biases in bias registers
    myIMU.calibrateMPU9250(myIMU.gyroBias, myIMU.accelBias);
    Serial.print("Gyro bias X: "); Serial.println(myIMU.gyroBias[0]);
    Serial.print("Gyro bias y: "); Serial.println(myIMU.gyroBias[1]);
    Serial.print("Gyro bias z: "); Serial.println(myIMU.gyroBias[2]);
    Serial.print("Accel bias X: "); Serial.println(myIMU.accelBias[0]);
    Serial.print("Accel bias y: "); Serial.println(myIMU.accelBias[1]);
    Serial.print("Accel bias z: "); Serial.println(myIMU.accelBias[2]);

    myIMU.initMPU9250();
    // Initialize device for active mode read of acclerometer, gyroscope, and
    // temperature
    Serial.println("MPU9250 initialized for active data mode....");

    // Read the WHO_AM_I register of the magnetometer, this is a good test of
    // communication
    byte d = myIMU.readByte(AK8963_ADDRESS, WHO_AM_I_AK8963);
    Serial.print("AK8963 "); Serial.print("I AM "); Serial.print(d, HEX);
    Serial.print(" I should be "); Serial.println(0x48, HEX);

    // Get magnetometer calibration from AK8963 ROM
    myIMU.initAK8963(myIMU.magCalibration);
    // Initialize device for active mode read of magnetometer
    Serial.println("AK8963 initialized for active data mode....");
    if (SerialDebug)
    {
      //  Serial.println("Calibration values: ");
      Serial.print("X-Axis sensitivity adjustment value ");
      Serial.println(myIMU.magCalibration[0], 2);
      Serial.print("Y-Axis sensitivity adjustment value ");
      Serial.println(myIMU.magCalibration[1], 2);
      Serial.print("Z-Axis sensitivity adjustment value ");
      Serial.println(myIMU.magCalibration[2], 2);
    }
  } // if (c == EXPECTED_MP9250_WHO_AM_I)
  else
  {
    Serial.print("Could not connect to MPU9250: 0x");
    Serial.println(c, HEX);
    while(1) ; // Loop forever if communication doesn't happen
  }
}

void collect9dofData(DiscData &discData) {
  // If intPin goes high, all data registers have new data
  // On interrupt, check if data ready interrupt
  if (myIMU.readByte(MPU9250_ADDRESS, INT_STATUS) & 0x01) {
    myIMU.readAccelData(myIMU.accelCount);  // Read the x/y/z adc values
    myIMU.getAres();

    // Now we'll calculate the accleration value into actual g's
    // This depends on scale being set
    myIMU.ax = (float)myIMU.accelCount[0]*myIMU.aRes*1000; // - accelBias[0]; Multiple by 1000 to convert to mG
    myIMU.ay = (float)myIMU.accelCount[1]*myIMU.aRes*1000; // - accelBias[1];
    myIMU.az = (float)myIMU.accelCount[2]*myIMU.aRes*1000; // - accelBias[2];

    myIMU.readGyroData(myIMU.gyroCount);  // Read the x/y/z adc values
    myIMU.getGres();

    // Calculate the gyro value into actual degrees per second
    // This depends on scale being set
    myIMU.gx = (float)myIMU.gyroCount[0]*myIMU.gRes;
    myIMU.gy = (float)myIMU.gyroCount[1]*myIMU.gRes;
    myIMU.gz = (float)myIMU.gyroCount[2]*myIMU.gRes;

    myIMU.readMagData(myIMU.magCount);  // Read the x/y/z adc values
    myIMU.getMres();
    // User environmental x-axis correction in milliGauss, should be
    // automatically calculated
    myIMU.magbias[0] = +470.;
    // User environmental x-axis correction in milliGauss TODO axis??
    myIMU.magbias[1] = +120.;
    // User environmental x-axis correction in milliGauss
    myIMU.magbias[2] = +125.;

    // Calculate the magnetometer values in milliGauss
    // Include factory calibration per data sheet and user environmental
    // corrections
    // Get actual magnetometer value, this depends on scale being set
    myIMU.mx = (float)myIMU.magCount[0]*myIMU.mRes*myIMU.magCalibration[0] -
               myIMU.magbias[0];
    myIMU.my = (float)myIMU.magCount[1]*myIMU.mRes*myIMU.magCalibration[1] -
               myIMU.magbias[1];
    myIMU.mz = (float)myIMU.magCount[2]*myIMU.mRes*myIMU.magCalibration[2] -
               myIMU.magbias[2];
  } // if (readByte(MPU9250_ADDRESS, INT_STATUS) & 0x01)

  // Must be called before updating quaternions!
  myIMU.updateTime();

  // Sensors x (y)-axis of the accelerometer is aligned with the y (x)-axis of
  // the magnetometer; the magnetometer z-axis (+ down) is opposite to z-axis
  // (+ up) of accelerometer and gyro! We have to make some allowance for this
  // orientationmismatch in feeding the output to the quaternion filter. For the
  // MPU-9250, we have chosen a magnetic rotation that keeps the sensor forward
  // along the x-axis just like in the LSM9DS0 sensor. This rotation can be
  // modified to allow any convenient orientation convention. This is ok by
  // aircraft orientation standards! Pass gyro rate as rad/s
  //  MadgwickQuaternionUpdate(ax, ay, az, gx*PI/180.0f, gy*PI/180.0f, gz*PI/180.0f,  my,  mx, mz);
  MahonyQuaternionUpdate(myIMU.ax, myIMU.ay, myIMU.az, myIMU.gx*DEG_TO_RAD,
                         myIMU.gy*DEG_TO_RAD, myIMU.gz*DEG_TO_RAD, myIMU.my,
                         myIMU.mx, myIMU.mz, myIMU.deltat);


  // Serial print and/or display at 0.5 s rate independent of data rates
  myIMU.delt_t = millis() - myIMU.count;

  // Define output variables from updated quaternion---these are Tait-Bryan
  // angles, commonly used in aircraft orientation. In this coordinate system,
  // the positive z-axis is down toward Earth. Yaw is the angle between Sensor
  // x-axis and Earth magnetic North (or true North if corrected for local
  // declination, looking down on the sensor positive yaw is counterclockwise.
  // Pitch is angle between sensor x-axis and Earth ground plane, toward the
  // Earth is positive, up toward the sky is negative. Roll is angle between
  // sensor y-axis and Earth ground plane, y-axis up is positive roll. These
  // arise from the definition of the homogeneous rotation matrix constructed
  // from quaternions. Tait-Bryan angles as well as Euler angles are
  // non-commutative; that is, the get the correct orientation the rotations
  // must be applied in the correct order which for this configuration is yaw,
  // pitch, and then roll.
  // For more see
  // http://en.wikipedia.org/wiki/Conversion_between_quaternions_and_Euler_angles
  // which has additional links.
  myIMU.yaw   = atan2(2.0f * (*(getQ()+1) * *(getQ()+2) + *getQ() *
                *(getQ()+3)), *getQ() * *getQ() + *(getQ()+1) * *(getQ()+1)
                - *(getQ()+2) * *(getQ()+2) - *(getQ()+3) * *(getQ()+3));
  myIMU.pitch = -asin(2.0f * (*(getQ()+1) * *(getQ()+3) - *getQ() *
                *(getQ()+2)));
  myIMU.roll  = atan2(2.0f * (*getQ() * *(getQ()+1) + *(getQ()+2) *
                *(getQ()+3)), *getQ() * *getQ() - *(getQ()+1) * *(getQ()+1)
                - *(getQ()+2) * *(getQ()+2) + *(getQ()+3) * *(getQ()+3));
  myIMU.pitch *= RAD_TO_DEG;
  myIMU.yaw   *= RAD_TO_DEG;
  // Declination of SparkFun Electronics (40°05'26.6"N 105°11'05.9"W) is
  // 	8° 30' E  ± 0° 21' (or 8.5°) on 2016-07-19
  // - http://www.ngdc.noaa.gov/geomag-web/#declination
  myIMU.yaw   -= 0.62;
  myIMU.roll  *= RAD_TO_DEG;

  discData.initDiscData(myIMU.ax,myIMU.ay,myIMU.az,
                            myIMU.gx,myIMU.gy,myIMU.gz,
                            myIMU.mx,myIMU.my,myIMU.mz,
                            myIMU.yaw,myIMU.pitch,myIMU.roll);
  
  if ((millis() - lastPublishTime) >= PUBLISH_INTERVAL) {
    Particle.publish("discbit_data", discData.generateJson(), 60, PRIVATE);

    lastPublishTime = millis();
  }
  
  // update LCD once per half-second independent of read rate
  if (myIMU.delt_t > 500) {
    if(SerialDebug) {
      Serial.print("ax = "); Serial.printf("%8.2f",myIMU.ax);
      Serial.print("    ay = "); Serial.printf("%8.2f",myIMU.ay);
      Serial.print("    az = "); Serial.printf("%8.2f",myIMU.az);
      Serial.println(" mg");

      Serial.print("gx = "); Serial.printf("%8.2f", myIMU.gx);
      Serial.print("    gy = "); Serial.printf("%8.2f", myIMU.gy);
      Serial.print("    gz = "); Serial.printf("%8.2f", myIMU.gz);
      Serial.println(" deg/s");

      Serial.print("mx = "); Serial.printf("%8.2f", myIMU.mx );
      Serial.print("    my = "); Serial.printf("%8.2f", myIMU.my );
      Serial.print("    mz = "); Serial.printf("%8.2f", myIMU.mz );
      Serial.println(" mG");

      Serial.print("q0 = "); Serial.printlnf("%8.2f",*getQ());
      Serial.print("qx = "); Serial.printf("%8.2f",*(getQ() + 1));
      Serial.print("    qy = "); Serial.printf("%8.2f",*(getQ() + 2));
      Serial.print("    qz = "); Serial.printlnf("%8.2f",*(getQ() + 3));

      Serial.print("Yaw = "); Serial.printf("%8.2f", myIMU.yaw);
      Serial.print("    Pitch = "); Serial.printf("%8.2f", myIMU.pitch);
      Serial.print("    Roll = "); Serial.printlnf("%8.2f", myIMU.roll);

      Serial.print("rate = ");
      Serial.print((float)myIMU.sumCount/myIMU.sum, 2);
      Serial.println(" Hz");
    }

    myIMU.count = millis();
    myIMU.sumCount = 0;
    myIMU.sum = 0;
  }
}
