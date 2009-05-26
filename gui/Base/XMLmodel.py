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
This module defines the XML data model in which the user defines the physical
options of the treated case. This module defines also a very usefull
function for the NavigatorTree display updating, taking into account
the current options selected by the user.

This module contains the following classes and function:
- XMLmodel
- XMLmodelTestCase
"""

#-------------------------------------------------------------------------------
# Library modules import
#-------------------------------------------------------------------------------


import sys, unittest


#-------------------------------------------------------------------------------
# Application modules import
#-------------------------------------------------------------------------------


from XMLvariables import Variables
import Toolbox


#-------------------------------------------------------------------------------
# class XMLmodel
#-------------------------------------------------------------------------------


class XMLmodel(Variables):
    """
    This class initialize the XML contents of the case.
    """
    def __init__(self, case):
        """
        """
        self.case = case
        self.root = self.case.root()
        self.node_models = self.case.xmlGetNode('thermophysical_models')

#FIXME: voir getTurbulenceModel de Turbulence.py (le noeud etant a declarer des le deaprt /...
    def getTurbModel(self):
        """
        This method return the turbilence model, but does not manage
        default values. Therefore...
        """
        nodeTurb = self.node_models.xmlGetNode('turbulence', 'model')
        return nodeTurb, nodeTurb['model']


    def getTurbVariable(self):
        """
        """
        nodeTurb, model = self.getTurbModel()
        nodeList = []

        if model in ('k-epsilon', 'k-epsilon-PL'):
            nodeList.append(nodeTurb.xmlGetNode('variable', name='turb_k'))
            nodeList.append(nodeTurb.xmlGetNode('variable', name='turb_eps'))

        elif model in ('Rij-epsilon', 'Rij-SSG'):
            for var in ('component_R11', 'component_R22', 'component_R33',
                        'component_R12', 'component_R13', 'component_R23',
                        'turb_eps'):
                nodeList.append(nodeTurb.xmlGetNode('variable', name=var))

        elif model in ('v2f-phi'):
            for var in ('turb_k', 'turb_eps', 'turb_phi', 'turb_fb'):
                nodeList.append(nodeTurb.xmlGetNode('variable', name=var))

        elif model in ('k-omega-SST'):
            nodeList.append(nodeTurb.xmlGetNode('variable', name='turb_k'))
            nodeList.append(nodeTurb.xmlGetNode('variable', name='turb_omega'))

        return nodeList


    def getTurbProperty(self):
        """
        """
        nodeTurb, model = self.getTurbModel()
        nodeList = []

        if model in ('mixing_length',
                     'k-epsilon',
                     'k-epsilon-PL',
                     'k-omega-SST',
                     'v2f-phi',
                     'Rij-epsilon',
                     'Rij-SSG'):
            nodeList.append(nodeTurb.xmlGetNode('property', name='turb_viscosity'))

        elif model in ('LES_Smagorinsky', 'LES_dynamique'):
            nodeList.append(nodeTurb.xmlGetNode('property', name='smagorinsky_constant'))

        return nodeList

#FIXME: A METTRE en commentaire  mais aussi getTurbVariable et getTurbProperty
    def getTurbNodeList(self):
        """
        """
        nodeList = []
        for node in self.getTurbVariable(): nodeList.append(node)
        for node in self.getTurbProperty(): nodeList.append(node)

        return nodeList



#-------------------------------------------------------------------------------
# XMLmodel test case
#-------------------------------------------------------------------------------

class ModelTest(unittest.TestCase):
    """
    Class beginning class test case of all pages
    """
    def setUp(self):
        """This method is executed before all "check" methods."""
        from Base.XMLengine import Case, XMLDocument
        from Base.XMLinitialize import XMLinit
        Toolbox.GuiParam.lang = 'en'
        self.case = Case(None)
        XMLinit(self.case)
        self.doc = XMLDocument()

    def tearDown(self):
        """This method is executed after all "check" methods."""
        del self.case
        del self.doc

    def xmlNodeFromString(self, string):
        """Private method to return a xml node from string"""
        n = self.doc.parseString(string)
        self.doc.xmlCleanAllBlank(n)
        return self.doc.root()


#-------------------------------------------------------------------------------
# XMLmodel test case
#-------------------------------------------------------------------------------


class XMLmodelTestCase(unittest.TestCase):
    """
    """
    def setUp(self):
        """
        This method is executed before all "check" methods.
        """
        from Base.XMLengine import Case
        from Base.XMLinitialize import XMLinit
        Toolbox.GuiParam.lang = 'en'
        self.case = Case(None)
        XMLinit(self.case)

    def tearDown(self):
        """
        This method is executed after all "check" methods.
        """
        del self.case

    def checkXMLmodelInstantiation(self):
        """
        Check whether the Case class could be instantiated
        """
        xmldoc = None
        xmldoc = XMLmodel(self.case)
        assert xmldoc != None, 'Could not instantiate XMLmodel'

##    def checkGetThermalModel(self):
##        """
##        Check whether the thermal model could be get.
##        """
##        doc = XMLmodel(self.case)
##        node_coal = self.case.xmlGetNode('pulverized_coal', 'model')
##        node_coal['model'] = "toto"
##        nodeThermal, model = doc.getThermalModel()
##
##        assert nodeThermal == node_coal, \
##               'Could not use the getThermalModel method'
##        assert model == "toto", \
##               'Could not use the getThermalModel method'

##    def checkGetThermoPhysicalModel(self):
##        """
##        Check whether the specific thermophysical model could be get.
##        """
##        doc = XMLmodel(self.case)
##        node_coal = self.case.xmlGetNode('pulverized_coal', 'model')
##        node_coal['model'] = "toto"
##        model = doc.getThermoPhysicalModel()
##
##        assert model == node_coal.el.tagName, \
##               'Could not use the getTermoPhysicalModel method'

def suite():
    testSuite = unittest.makeSuite(XMLmodelTestCase, "check")
    return testSuite

def runTest():
    print "XMLmodelTestCase to be completed..."
    runner = unittest.TextTestRunner()
    runner.run(suite())


#-------------------------------------------------------------------------------
# End of XMLmodel
#-------------------------------------------------------------------------------