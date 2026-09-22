>**Note:**
      All Datasheets should also be availible under [docs/datasheets](../tree/main/docs/datasheets)

## 📦 Core Components

<table>
  <thead>
    <tr>
      <th>🔧 Component</th>
      <th>📄 Description</th>
      <th>🎨 Color</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td>🧠 <strong>SoC / CPU</strong></td>
      <td><a href="https://people.debian.org/~glaubitz/156346062-STi7105.pdf">STi7105-KUD</a><br><a href="https://www.linuxtv.org/wiki/index.php/STMicroelectronics">LinuxTV Info</a></td>
      <td>Red</td>
    </tr>
    <tr>
      <td>💾 <strong>RAM</strong></td>
      <td>2× <a href="http://alldatasheet.net/datasheet-pdf/view/458044/ELPIDA/EDE2116AEBG-8E-F.html">EDE2116ACBG (512Mb)</a></td>
      <td>Blue</td>
    </tr>
    <tr>
      <td>📦 <strong>NOR Flash</strong></td>
      <td><a href="https://www.mouser.de/datasheet/3/70/1/Infineon_S29GL064N_S29GL032N_64_Mbit_32_Mbit_3_V_Page_Mode_MirrorBit_Flash_DataSheet_v03_00_EN.pdf">GL064N90FFIS4 – 8MB</a></td>
      <td>Yellow</td>
    </tr>
    <tr>
      <td>📦 <strong>NAND Flash</strong></td>
      <td><a href="https://www.alldatasheet.com/datasheet-pdf/view/227964/NUMONYX/NAND02GW3B2D.html">NAND02GW3B2DZA6 – 256MB</a></td>
      <td>Orange</td>
    </tr>
    <tr>
      <td>💳 <strong>Smartcard Interface</strong></td>
      <td>2× <a href="https://www.st.com/resource/en/datasheet/st8024.pdf">ST8024CDR</a></td>
      <td>Green</td>
    </tr>
    <tr>
      <td>📡 <strong>Satellite tuner IC</strong></td>
      <td>2x <a href="https://www.st.com/resource/en/data_brief/cd00207925.pdf">STV6110A</a><br><a href="https://www.linuxtv.org/wiki/index.php/ST_STV6110A">LinuxTV Info</a></td>
      <td>Pink</td>
    </tr>
    <tr>
      <td>📡 <strong>DVB-S2 FEC</strong></td>
      <td><a href="https://www.st.com/resource/en/data_brief/stv0900.pdf">STV0900B</a><br><a href="https://www.linuxtv.org/wiki/index.php/STMicroelectronics_STV0900">LinuxTV Info</a></td>
      <td>Cyan</td>
    </tr>
    <tr>
      <td>🔁 <strong>CPLD</strong></td>
      <td><a href="https://www.mouser.de/datasheet/2/612/max2_mii5v1-1299433.pdf">EPM1270F256C5N</a></td>
      <td>Purple</td>
    </tr>
  </tbody>
</table>

---

## 🔌 Additional Components

<table>
  <thead>
    <tr>
      <th>🔧 Component</th>
      <th>📄 Description</th>
      <th>🎨 Color</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td>🔗 <strong>USB/SD Hub</strong></td>
      <td><a href="https://www.mouser.de/datasheet/2/268/USB2640_USB2641_Data_Sheet_DS00001947C-3500243.pdf">USB2641</a></td>
      <td>Silver</td>
    </tr>
    <tr>
      <td>🔋 <strong>Buck Converter</strong></td>
      <td><a href="https://www.alldatasheet.com/datasheet-pdf/view/188828/MPS/MP2307DN.html">MP2307DN</a></td>
      <td>Brown</td>
    </tr>
    <tr>
      <td>🛡️ <strong>HDMI Protection</strong></td>
      <td><a href="https://www.onsemi.com/pdf/datasheet/cm2020-00tr-d.pdf">CM2020-00TR</a></td>
      <td>Teal</td>
    </tr>
    <tr>
      <td>🎚 <strong>A/V Buffer</strong></td>
      <td><a href="https://www.alldatasheet.com/datasheet-pdf/view/472635/STMICROELECTRONICS/STV6440.html">STV6440</a></td>
      <td>Coral</td>
    </tr>
    <tr>
      <td>🌐 <strong>Ethernet PHY</strong></td>
      <td><a href="https://ww1.microchip.com/downloads/en/DeviceDoc/00002164B.pdf">LAN8710A</a></td>
      <td>Olive</td>
    </tr>
    <tr>
      <td>📡 <strong>LNB Power</strong></td>
      <td>2x <a href="https://www.alldatasheet.com/datasheet-pdf/view/169391/ALLEGRO/A8290.html">A8290T</a></td>
      <td>Maroon</td>
    </tr>
  </tbody>
</table>

---

## ⚙️ Minor Components

<table>
  <thead>
    <tr>
      <th>🔧 Component</th>
      <th>📄 Description</th>
      <th>🎨 Color</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td><strong>RT9712</strong></td>
      <td><a href="https://www1.futureelectronics.com/doc/RICHTEK/RT9712CGF.pdf">RT9712CGF</a></td>
      <td>Slate Gray</td>
    </tr>
  </tbody>
</table>

---

## On-Board Connectors & Headers

The mainboard contains several unpopulated pin headers. The names `J1`, `J2`, and `J3` have been assigned by this project for documentation purposes and are **not printed on the PCB**. The colors are used to identify them in project-related images.

<table>
  <thead>
    <tr>
      <th>🔧 Connector</th>
      <th>🎨 Color</th>
      <th>📄 Description</th>
      <th>📌 Status / Notes</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td><strong>J1</strong> (Unlabeled 4-Pin)</td>
      <td>Light Blue</td>
      <td>UART Serial Port (Transmit-Only)</td>
      <td>✅ Active. Transmits a detailed bootlog and system messages. User input (RX) appears to be ignored. <a href="https://github.com/MaizeShark/TechniSat-S1/wiki/UART-Interface-(J1)-&-Bootlog">See details</a>.</td>
    </tr>
    <tr>
      <td><strong>J2</strong> (Unlabeled)</td>
      <td>Lime Green</td>
      <td><em>seems to be JTAG</em></td>
      <td>⚠️ To be determined.</td>
    </tr>
    <tr>
      <td><strong>J3</strong> (Unlabeled)</td>
      <td>Pink</td>
      <td><em>(Purpose unknown)</em></td>
      <td>⚠️ To be determined.</td>
    </tr>
  </tbody>
</table>

---

## 📝 Note

This documentation is for the technical analysis and reverse engineering of the Technisat Isio S1 receiver.  
Pull requests for additions and corrections are welcome.