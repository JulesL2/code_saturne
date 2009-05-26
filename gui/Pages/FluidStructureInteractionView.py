# -*- coding: iso-8859-1 -*-
#
#-------------------------------------------------------------------------------
#
#     This file is part of the Code_Saturne User Interface, element of the
#     Code_Saturne CFD tool.
#
#     Copyright (C) 1998-2009 EDF S.A., France
#
#     contact: saturne-support@edf.fr
#
#     The Code_Saturne User Interface is free software; you can redistribute it
#     and/or modify it under the terms of the GNU General Public License
#     as published by the Free Software Foundation; either version 2 of
#     the License, or (at your option) any later version.
#
#     The Code_Saturne User Interface is distributed in the hope that it will be
#     useful, but WITHOUT ANY WARRANTY; without even the implied warranty
#     of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#     GNU General Public License for more details.
#
#     You should have received a copy of the GNU General Public License
#     along with the Code_Saturne Kernel; if not, write to the
#     Free Software Foundation, Inc.,
#     51 Franklin St, Fifth Floor,
#     Boston, MA  02110-1301  USA
#
#-------------------------------------------------------------------------------

"""
This module defines the values of reference.

This module contains the following classes and function:
- FluidStructureInteractionAdvancedOptionsView
- StandardItemModel
- Coupling
- LineEditCoupling
- FormulaCoupling
- CheckBoxCoupling
- CouplingManager
- FluidStructureInteractionView
"""

#-------------------------------------------------------------------------------
# Library modules import
#-------------------------------------------------------------------------------

import logging

#-------------------------------------------------------------------------------
# Third-party modules
#-------------------------------------------------------------------------------

from PyQt4.QtCore import Qt, QVariant, QString, SIGNAL, pyqtSignature
from PyQt4.QtGui  import QDialog, QHeaderView, QStandardItemModel, QWidget 
from PyQt4.QtGui  import QAbstractItemView, QItemSelectionModel, QValidator 

try:
    import mei
    _have_mei = True
except ImportError:
    _have_mei = False

#-------------------------------------------------------------------------------
# Application modules import
#-------------------------------------------------------------------------------
from Pages.FluidStructureInteractionForm  import Ui_FluidStructureInteractionForm
from Base                                 import QtPage
from Pages.FluidStructureInteractionModel import FluidStructureInteractionModel
from Pages.LocalizationModel              import LocalizationModel
from Pages.Boundary                       import Boundary
from Pages.FluidStructureInteractionAdvancedOptionsDialogForm import \
Ui_FluidStructureInteractionAdvancedOptionsDialogForm

if _have_mei:
    from Pages.QMeiEditorView import QMeiEditorView


#-------------------------------------------------------------------------------
# log config
#-------------------------------------------------------------------------------

logging.basicConfig()
log = logging.getLogger("FluidStructureInteractionView")
#log.setLevel(GuiParam.DEBUG)

#-------------------------------------------------------------------------------
# Constantes
#-------------------------------------------------------------------------------

displacement_prediction_alpha          = 'displacement_prediction_alpha'
displacement_prediction_beta           = 'displacement_prediction_beta'
stress_prediction_alpha                = 'stress_prediction_alpha'
external_coupling_post_synchronization = 'external_coupling_post_synchronization'
monitor_point_synchronisation          = 'monitor_point_synchronisation'

#-------------------------------------------------------------------------------
# Advanced dialog
#-------------------------------------------------------------------------------

class FluidStructureInteractionAdvancedOptionsView(QDialog,
                        Ui_FluidStructureInteractionAdvancedOptionsDialogForm):
    """
    Advanced dialog
    """
    def __init__(self, parent, default):
        """
        Constructor
        """
        # Init base classes
        QDialog.__init__(self, parent)
        Ui_FluidStructureInteractionAdvancedOptionsDialogForm.__init__(self)
        self.setupUi(self)

        title = self.tr("Displacements prediction:")
        self.setWindowTitle(title)

        self.__default = default
        self.__result  = default.copy()   
        self.__setValidator()
        self.__setInitialValues()


    def __setValidator(self):
        """
        Set the validator
        """
        validator = QtPage.DoubleValidator(self.lineEditDisplacementAlpha,
                                           min=0.0)
        self.lineEditDisplacementAlpha.setValidator(validator)

        validator = QtPage.DoubleValidator(self.lineEditDisplacementBeta, 
                                           min=0.0)
        self.lineEditDisplacementBeta.setValidator(validator)

        validator = QtPage.DoubleValidator(self.lineEditStressAlpha, min=0.0)
        self.lineEditStressAlpha.setValidator(validator)


    def __setInitialValues(self):
        """
        Set the initial values for the 4 widgets
        """
        # Read from default
        displacementAlpha = str(self.__default[displacement_prediction_alpha])
        displacementBeta  = str(self.__default[displacement_prediction_beta ])
        stressAlpha       = str(self.__default[stress_prediction_alpha      ])

        isSynchronizationOn = self.__default[monitor_point_synchronisation] == 'on'

        # Update Widget
        self.lineEditDisplacementAlpha.setText(displacementAlpha)
        self.lineEditDisplacementBeta.setText(displacementBeta)
        self.lineEditStressAlpha.setText(stressAlpha)
        self.checkBoxSynchronization.setChecked(isSynchronizationOn)


    def get_result(self):
        """
        Method to get the result
        """
        return self.__result


    def accept(self):
        """
        Method called when user clicks 'OK'
        """
        # Read value from widget
        displacementAlpha = float(self.lineEditDisplacementAlpha.text())
        displacementBeta  = float(self.lineEditDisplacementBeta.text())
        stressAlpha       = float(self.lineEditStressAlpha.text())

        if self.checkBoxSynchronization.isChecked():
            synchronization = 'on'
        else:
            synchronization = 'off'

        # Set result attributes
        self.__result[displacement_prediction_alpha] = displacementAlpha
        self.__result[displacement_prediction_beta ] = displacementBeta
        self.__result[stress_prediction_alpha      ] = stressAlpha
        self.__result[monitor_point_synchronisation] = synchronization

        QDialog.accept(self)


    def reject(self):
        """
        Method called when user clicks 'Cancel'
        """
        QDialog.reject(self)


    def tr(self, text):
        """
        Translation
        """
        return text



#-------------------------------------------------------------------------------
# StandarItemModel class
#-------------------------------------------------------------------------------
class StandardItemModel(QStandardItemModel):
    """
    StandardItemModel for table view
    """

    def __init__(self):
        """
        StandarItemModel for fluid structure interaction tableView
        """
        QStandardItemModel.__init__(self) 

        # Define header 
        self.headers = [self.tr("Structure number"),
                        self.tr("Label"),
                        self.tr("Location")]
        self.setColumnCount(len(self.headers))

        # Set attributes
        self.__data    = []


    def data(self, index, role):
        """
        Called when table need to read the data
        """
        # return value only for Qt.DisplayRole
        if index.isValid() and role == Qt.DisplayRole:
            row = index.row()
            col = index.column()
            return QVariant(self.__data[row][col])            

        return QVariant()


    def flags(self, index):
        """
        Define which column is editable/selectable/enable.
        """
        return Qt.ItemIsEnabled | Qt.ItemIsSelectable       


    def headerData(self, section, orientation, role):
        """
        Return the header column data.
        """
        if orientation == Qt.Horizontal and role == Qt.DisplayRole:
            return QVariant(self.headers[section])
        return QVariant()


    def setData(self, index, value, role):
        """
        Set a the data when table changed 
        """
        raise Exception('Cannot edit column')


    def addItem(self, zone):
        """
        Add an element in the table view.
        """       
        index = len(self.__data)
        line = [index + 1, zone.getLabel(), zone.getLocalization()]

        # Add the line and increment line number
        self.__data.append(line)
        row = self.rowCount()
        self.setRowCount(row + 1)


    def getLabel(self, index):
        """
        return the label
        """
        row = index.row()
        [index, label, localization] = self.__data[row]
        return label


#-------------------------------------------------------------------------------
# Coupling base class
#-------------------------------------------------------------------------------

class Coupling:
    """
    Coupling is the base class to manage all widgets which value depend on the 
    boundary.
    
    It provides getBoundaryDefinedValue/setBoundaryDefinedValue methods which 
    get/set the value of a boundary attribute. Getter and setter are specified
    int the constructor

    It also automatically enable/disable the widget when boundary is present 
    or not

    Derived class can override onBoundarySet method to specify the initial 
    value of the widget base on the boundary 
    """

    def __init__(self, widget, getterStr, setterStr ):
        """
        Constructor. getterStr and setterStr are string
        """
        self.__widget    = widget
        self.__getterStr = getterStr
        self.__setterStr = setterStr
        self.__boundary  = None

        # As no boundary is selected disable the widget
        widget.setEnabled(False)
               

    def getWidget(self):
        """
        Return the widget
        """
        return self.__widget


    def setBoundary(self, boundary):
        """
        Set the current boundary
        """
        self.__boundary = boundary
        
        #Enable widget
        self.__widget.setEnabled(True)

        # call onBoundarySet for derived class
        self.onBoundarySet()


    def onBoundarySet(self):
        """
        Called when boundary is set. Nothing by default
        """
        pass


    def getBoundaryDefinedValue(self):
        """
        Return the value of the boundary using the getter function
        """
        return getattr(self.__boundary, self.__getterStr)()


    def setBoundaryDefinedValue(self, value):
        """
        Set the value of the boundary using the setter function
        """
        getattr(self.__boundary, self.__setterStr)(value)



#-------------------------------------------------------------------------------
# LineEdit Coupling class
#-------------------------------------------------------------------------------

class LineEditCoupling(Coupling):
    """
    LineEdit that depend on a boundary
    """

    def __init__(self, lineEdit, getter, setter):
        """
        Constructor
        """
        Coupling.__init__(self, lineEdit, getter, setter)

        # Add validator.     
        validator = QtPage.DoubleValidator(lineEdit, min=0.0)
        lineEdit.setValidator(validator)
        lineEdit.connect(lineEdit, SIGNAL("textChanged(const QString &)"), 
                         self.__slotTextChanged)
        

    def onBoundarySet(self):
        """
        Called when boundary is set. Update lineEdit text
        """
        value  = self.getBoundaryDefinedValue()
        self.getWidget().setText(str(value))
           
            
    @pyqtSignature("const QString&")
    def __slotTextChanged(self, text):
        """ 
        Update the model
        """
        self.setBoundaryDefinedValue(text)
        

#-------------------------------------------------------------------------------
# Formula Coupling class
#-------------------------------------------------------------------------------

class FormulaCoupling(Coupling):
    """
    Formula button that depend on a boundary
    """

    def __init__(self, button, getter, setter, default, required, symbols, examples):
        """
        Constructor
        """
        Coupling.__init__(self, button, getter, setter)

        self.__default  = default
        self.__required = required
        self.__examples = examples
        self.__symbols  = symbols
        button.connect(button, SIGNAL("clicked(bool)"), self.__slotFormula)


    def onBoundarySet(self):
        """
        Called when boundary is set. 
        """
        # call getter to create default value if needed
        self.getBoundaryDefinedValue()
        
        # Disable button if _have_mei is false
        if not _have_mei:
            self.getWidget().setEnabled(False)


    @pyqtSignature("const QString&")
    def __slotFormula(self, text):
        """
        Run formula editor.
        """
        # Read current expression
        exp = self.getBoundaryDefinedValue()

        if not exp:
            exp = self.__default
        
        # run the editor           
        dialog = QMeiEditorView(self.getWidget(),
                                expression = exp,
                                required   = self.__required,
                                symbols    = self.__symbols,
                                examples   = self.__examples)
        if dialog.exec_():
            result = dialog.get_result()
            log.debug("FormulaCoupling -> %s" % str(result))
            self.setBoundaryDefinedValue(result)



#-------------------------------------------------------------------------------
# CheckBoxCouplings Coupling class
#-------------------------------------------------------------------------------
class CheckBoxCoupling(Coupling):
    """
    CheckBox that depend on a boundary
    """

    def __init__(self, checkBox, getter, setter):
        """
        Constructor
        """
        Coupling.__init__(self, checkBox, getter, setter)
        checkBox.connect(checkBox, SIGNAL("stateChanged(int)"), 
                         self.__slotStateChanged)


    def onBoundarySet(self):
        """
        Called when boundary is set.
        """
        value = self.getBoundaryDefinedValue()

        if value == "on":
            state = Qt.Checked
        else:
            state = Qt.Unchecked
        self.getWidget().setCheckState(state)


    @pyqtSignature("int")
    def __slotStateChanged(self, state):
        """
        Called when checkbox state changed
        """
        value = "off"
        if state == Qt.Checked:
            value = "on"           
        self.setBoundaryDefinedValue(value)


#-------------------------------------------------------------------------------
# CouplingManager class
#-------------------------------------------------------------------------------
class CouplingManager:
    """
    Manage and initialize coupling derived objects
    """

    def __init__(self, mainView, case, internalTableModel, externalTableModel):
        """
        Constructor
        """
        self.__case               = case
        self.__internalTableModel = internalTableModel
        self.__externalTableModel = externalTableModel
        self.__internalCouplings = []
        self.__externalCouplings = []

        # Init widgets
        self.__initLineEditCouplings(mainView)
        self.__initFormulaCouplings (mainView)
        self.__initCheckBoxCouplings(mainView)


    def __initLineEditCouplings(self, mainView):
        """
        Initialize the creation of LineEditCoupling
        """
        coupings = []
        coupings.append( LineEditCoupling( mainView.lineEditInitialDisplacementX
                       , "getInitialDisplacementX", "setInitialDisplacementX"))        
        coupings.append( LineEditCoupling( mainView.lineEditInitialDisplacementY
                       , "getInitialDisplacementY", "setInitialDisplacementY"))        
        coupings.append( LineEditCoupling( mainView.lineEditInitialDisplacementZ
                       , "getInitialDisplacementZ", "setInitialDisplacementZ"))
                                                
        coupings.append( 
            LineEditCoupling( mainView.lineEditEquilibriumDisplacementX,
                              "getEquilibriumDisplacementX",
                              "setEquilibriumDisplacementX"))        

        coupings.append( 
            LineEditCoupling( mainView.lineEditEquilibriumDisplacementY,
                              "getEquilibriumDisplacementY",
                              "setEquilibriumDisplacementY"))        
        coupings.append( 
            LineEditCoupling( mainView.lineEditEquilibriumDisplacementZ,
                              "getEquilibriumDisplacementZ",
                              "setEquilibriumDisplacementZ"))        

        coupings.append( LineEditCoupling( mainView.lineEditInitialVelocityX
                       , "getInitialVelocityX", "setInitialVelocityX"))
        coupings.append( LineEditCoupling( mainView.lineEditInitialVelocityY
                       , "getInitialVelocityY", "setInitialVelocityY"))        
        coupings.append( LineEditCoupling( mainView.lineEditInitialVelocityZ
                       , "getInitialVelocityZ", "setInitialVelocityZ"))                
        self.__internalCouplings.extend(coupings)


    def __initCheckBoxCouplings(self, mainView):
        """
        Initialize the creation of the checkbox coupling
        """
        couplings = []
        couplings.append(CheckBoxCoupling( mainView.checkBoxDDLX, "getDDLX",
                                           "setDDLX"))
        couplings.append(CheckBoxCoupling( mainView.checkBoxDDLY, "getDDLY", 
                                           "setDDLY"))
        couplings.append(CheckBoxCoupling( mainView.checkBoxDDLZ, "getDDLZ", 
                                           "setDDLZ"))
        self.__externalCouplings.extend(couplings)


    def __initFormulaCouplings(self, mainView):
        """
        Initialize the creation of the formula button
        """
        default = "m11 = ;"
        examples = "m11 = 100;\nm22 = 100;\nm33 = 100;"
        examples += "\nm12 = 0;\nm13 = 0;\nm23 = 0;\nm21 = 0;\nm31 = 0;\nm32 = 0;"
        massRequired = [('m11', 'mass matrix of the structure (1,1)'),
                        ('m22', 'mass matrix of the structure (2,2)'),
                        ('m33', 'mass matrix of the structure (3,3)'),
                        ('m12', 'mass matrix of the structure (1,2)'),
                        ('m13', 'mass matrix of the structure (1,3)'),
                        ('m23', 'mass matrix of the structure (2,3)'),
                        ('m21', 'mass matrix of the structure (2,1)'),
                        ('m31', 'mass matrix of the structure (3,1)'),
                        ('m32', 'mass matrix of the structure (3,2)')]
        symbols = [('dt', 'time step'),
                   ('t', 'current time'),
                   ('nbIter', 'number of iteration')]

        stiffnessRequired = []
        dampingRequired = []
        for var, str in massRequired:
            stiffnessRequired.append( (var, str.replace('mass', 'stiffness') ) )
            dampingRequired.append( (var, str.replace('mass', 'damping') ) )

        couplings = []
        couplings.append( FormulaCoupling( mainView.pushButtonMassMatrix,
                                           "getMassMatrix", "setMassMatrix", 
                                           default, massRequired, symbols, 
                                           examples))
        couplings.append( FormulaCoupling( mainView.pushButtonStiffnessMatrix, 
                                           "getStiffnessMatrix", 
                                           "setStiffnessMatrix", default, 
                                           stiffnessRequired, symbols, examples))

        couplings.append( FormulaCoupling( mainView.pushButtonDampingMatrix, 
                                          "getDampingMatrix", 
                                          "setDampingMatrix", default, 
                                          dampingRequired, symbols, examples))

        defaultFluidForce  = "Fx = "
        requiredFluidForce = [('Fx', 'Force applied to the structure along X'),
                              ('Fy', 'Force applied to the structure along Y'),
                              ('Fz', 'Force applied to the structure along Z')]
        symbolsFluidForce = symbols[:];
        symbolsFluidForce.append( ('Fluid_Fx', 'Force of flow along X'))
        symbolsFluidForce.append( ('Fluid_Fy', 'Force of flow along Y'))
        symbolsFluidForce.append( ('Fluid_Fz', 'Force of flow along Z'))

        examplesFluidForce = "Fx = Fluid_Fx;\nFy = Fluid_Fy;\nFz = Fluid_Fz;"
        couplings.append( FormulaCoupling( mainView.pushButtonFluidForce, 
                                           "getFluidForceMatrix",
                                           "setFluidForceMatrix", 
                                           defaultFluidForce,
                                           requiredFluidForce, 
                                           symbolsFluidForce,
                                           examplesFluidForce))
        self.__internalCouplings.extend(couplings)



    @pyqtSignature("QModelIndex  const&, QModelIndex  const&")
    def slotInternalSelectionChanged(self, selected, deselected):
        """
        Called when internal tableView selection changed
        """
        self.__selectionChanged(self.__internalTableModel, 
                               self.__internalCouplings, selected)


    @pyqtSignature("QModelIndex  const&, QModelIndex  const&")
    def slotExternalSelectionChanged(self, selected, deselected):
        """
        Called when external tableView selection changed
        """
        self.__selectionChanged(self.__externalTableModel, 
                               self.__externalCouplings, selected)


    def __selectionChanged(self, tableModel, couplings, selected):
        """
        Called when a tableView selection changed
        """
        # Get Boundary
        label = tableModel.getLabel(selected)

        boundary = Boundary("coupling_mobile_boundary", label, self.__case)

        # Set boundary for coupling
        for coupling in couplings:
            coupling.setBoundary(boundary)


#-------------------------------------------------------------------------------
# Main class
#-------------------------------------------------------------------------------
class FluidStructureInteractionView(QWidget, Ui_FluidStructureInteractionForm):
    """
    Main class.
    """

    def __init__(self, parent, case):
        """
        Constructor
        """
        # Init base classes
        QWidget.__init__(self, parent)
        
        Ui_FluidStructureInteractionForm.__init__(self)
        self.setupUi(self)

        self.__case = case
        self.__model = FluidStructureInteractionModel(case)

        self.__defineConnection()
        self.__addValidators()
        self.__setInitialValues()

        # Use localization model for column 0, 1, 3        
        modelLocalization  = LocalizationModel("BoundaryZone", case)

        # Store modelLocalization as attribut to avoid garbage collector to clean it
        self.__modelLocalization = modelLocalization

        # Initialize the internal and external TableViewItemModel           
        self.__internalTableModel = self.__createTableViewItemModel(modelLocalization, 
                                                                    'internal_coupling')
        self.__externalTableModel = self.__createTableViewItemModel(modelLocalization,
                                                                    'external_coupling')
        
        # Coupling Manager
        couplingManager = CouplingManager(self, case, self.__internalTableModel
                                         , self.__externalTableModel)
        # Avoid garbage collector to delete couplingManager        
        self.__couplingManager = couplingManager
        
        # Initialize internal / external table view
        self.__initTableView(self.tableInternalCoupling,
                            self.__internalTableModel,
                            couplingManager.slotInternalSelectionChanged)

        self.__initTableView(self.tableExternalCoupling, 
                            self.__externalTableModel,
                            couplingManager.slotExternalSelectionChanged)
       

    def __defineConnection(self):
        """
        Define coonection for widget that do not depend on the boundary
        """
        self.connect(self.lineEditNALIMX, 
                     SIGNAL("textChanged(const QString &)"), self.__slotNalimx)

        self.connect(self.lineEditEPALIM, 
                     SIGNAL("textChanged(const QString &)"), self.__slotEpalim)
        self.connect(self.pushButtonAdvanced, 
                     SIGNAL("clicked(bool)"), self.__slotAdvanced)
        self.connect(self.checkBoxPostSynchronization, 
                     SIGNAL("stateChanged (int)"), 
                     self.__slotPostSynchronization)


    def __addValidators(self):
        """
        Add the validator for NALIMX and EPALIM
        """
        validatorNALIMX = QtPage.IntValidator(self.lineEditNALIMX, min=1)
        self.lineEditNALIMX.setValidator(validatorNALIMX)

        validatorEPALIM = QtPage.DoubleValidator(self.lineEditEPALIM, min=0.0)
        validatorEPALIM.setExclusiveMin(True)
        self.lineEditEPALIM.setValidator(validatorEPALIM)


    def __setInitialValues(self):
        """
        Set Widget initial values that do not depend on the boundary
        """
        nalimx = self.__model.getMaxIterations()
        self.lineEditNALIMX.setText(QString(str(nalimx)))
        epalim = self.__model.getPrecision()
        self.lineEditEPALIM.setText(QString(str(epalim)))
        postSynchronization      = self.__model.getExternalCouplingPostSynchronization()
        self.checkBoxPostSynchronization.setChecked(postSynchronization == 'on')
    

    def __createTableViewItemModel(self, modelLocalization, filterALE):
        """
        Create the table view item model
        """
        tableViewItemModel = StandardItemModel()

        # Populate QTableView model
        for zone in modelLocalization.getZones():
            boundary = Boundary("mobile_boundary", zone.getLabel(), self.__case)
            if boundary.getALEChoice() == filterALE:
                tableViewItemModel.addItem(zone)
        return tableViewItemModel


    def __initTableView(self, tableView, tableViewItemModel, slotSelectionChanged):
        """
        Initialize the main table view
        """
        # Set the model
        tableView.setModel(tableViewItemModel)
         
        # set the column size
        tableView.verticalHeader().setResizeMode(QHeaderView.ResizeToContents)
        tableView.horizontalHeader().setResizeMode(QHeaderView.ResizeToContents)
        tableView.horizontalHeader().setResizeMode(2, QHeaderView.Stretch)
        
        # Connect slot when selection changed
        tableView.setSelectionBehavior(QAbstractItemView.SelectRows)
        selectionModel = QItemSelectionModel(tableViewItemModel, tableView)
        tableView.setSelectionModel(selectionModel)

        self.connect(selectionModel, 
                     SIGNAL( "currentChanged(const QModelIndex &, const QModelIndex &)"),
                     slotSelectionChanged)


    @pyqtSignature("const QString&")
    def __slotNalimx(self, text):
        """
        Input viscosity type of mesh : isotrop or orthotrop.
        """
        nalimx, ok = text.toInt()
        if self.sender().validator().state == QValidator.Acceptable:
            self.__model.setMaxIterations(nalimx)
 
 
    @pyqtSignature("const QString&")
    def __slotEpalim(self, text):
        """
        Input viscosity type of mesh : isotrop or orthotrop.
        """
        epalim, ok = text.toDouble()
        if self.sender().validator().state == QValidator.Acceptable:
            self.__model.setPrecision(epalim)


    @pyqtSignature("int")
    def __slotPostSynchronization(self, value):
        """
        Called when check box state changed
        """
        postSynchronization = 'on'
        if not value:
            postSynchronization = 'off'
        self.__model.setExternalCouplingPostSynchronization(postSynchronization)



    @pyqtSignature("const QString&")
    def __slotAdvanced(self, text):
        """
        Private slot.
        Ask one popup for advanced specifications
        """
        # Set the default value
        default = {}
        default[displacement_prediction_alpha] = self.__model.getDisplacementPredictionAlpha()
        default[displacement_prediction_beta ] = self.__model.getDisplacementPredictionBeta()
        default[stress_prediction_alpha      ] = self.__model.getStressPredictionAlpha()
        default[monitor_point_synchronisation] = \
                            self.__model.getMonitorPointSynchronisation()
        log.debug("slotAdvancedOptions -> %s" % str(default))

        # run the dialog
        dialog = FluidStructureInteractionAdvancedOptionsView(self, default)
        if dialog.exec_():
            # Set the model with the dialog results
            result = dialog.get_result()
            log.debug("slotAdvanced -> %s" % str(result))
            self.__model.setDisplacementPredictionAlpha(result[displacement_prediction_alpha])
            self.__model.setDisplacementPredictionBeta(result[displacement_prediction_beta])
            self.__model.setStressPredictionAlpha(result[stress_prediction_alpha])
            self.__model.setMonitorPointSynchronisation(result[monitor_point_synchronisation])


    def tr(self, text):
        """
        Translation
        """
        return text 

#-------------------------------------------------------------------------------
# End
#-------------------------------------------------------------------------------
